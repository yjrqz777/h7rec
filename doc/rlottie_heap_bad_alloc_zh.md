# STM32H7 rlottie bad_alloc / SIGABRT 内存问题排查记录

## 背景

在设备上播放 rlottie 动画时，运行一段时间后程序异常终止，调试输出类似：

```text
bad_alloc was thrown in -fno-exc
SIGABRT: Abnormal termination
```

这两行其实是同一件事的两个阶段：

- `bad_alloc was thrown in -fno-exc`：C++ 代码里 `new` / `malloc` 申请内存失败，本应抛出 `std::bad_alloc` 异常。
- `SIGABRT: Abnormal termination`：但工程用 ArmClang 编译且关闭了异常（`-fno-exceptions`），运行时没法真正抛异常，于是直接调用 `abort()`，表现为 SIGABRT。

所以这个报错的本质只有一句话：**C 库堆（C library heap）不够用了**。

## 三块堆是相互独立的，别搞混

这是排查时最容易踩的坑。工程里同时存在三块完全独立的“堆”，rlottie 的 `bad_alloc` 只跟其中一块有关：

| 堆 | 配置位置 | 当前大小 | 谁在用 |
|---|---|---|---|
| C 库堆 (`Heap_Size`) | `MDK-ARM/startup_stm32h743xx.s` | **0x10000 = 64KB** | rlottie 内部全部 `new` / `malloc` ← **bad_alloc 在这里** |
| LVGL 内存池 (`LV_MEM_SIZE`) | `App/Gui/lv_conf.h` | 128KB | `lv_rlottie` 渲染 surface、SD JSON 读取缓冲 |
| FreeRTOS 堆 | `Core/Inc/FreeRTOSConfig.h` | 32KB | 任务栈、内核对象 |

很多人看到 rlottie 报内存错，第一反应是去加大 `LV_MEM_SIZE` 或 FreeRTOS 堆，**这两个都改不到点子上**。rlottie 的解析与栅格化用的是标准 C/C++ 的 `new`/`malloc`，走的是启动文件里的 `Heap_Size`。

## 内存到底花在哪

一次 rlottie 播放，内存大致分布如下。

### 走 C 库堆（rlottie 内部，bad_alloc 的来源）

- Lottie JSON 解析后建立的模型对象树（layers / shapes / keyframes 等），层数和关键帧越多，占用越大。
- 栅格化阶段的临时缓冲：路径扫描线、mask、blend 的 scratch buffer。
- 这些是**瞬时峰值**，解析+首帧渲染时冲得最高，64KB 很容易被冲破。

### 走 LVGL 内存池（不是 bad_alloc 的来源）

在 `External/lvgl-8.4.0/src/extra/libs/rlottie/lv_rlottie.c` 里：

```c
size_t allocated_buf_size = (create_width * create_height * LV_ARGB32 / 8);
rlottie->allocated_buf = lv_mem_alloc(allocated_buf_size);
```

渲染 surface 是 `宽 × 高 × 4` 字节的 ARGB8888。当前 `App/Gui/app_gui.c`：

```c
#define RLOTTIE_DEMO_SIZE 80
```

即 `80 × 80 × 4 = 25600` 字节 ≈ 25KB，从 128KB 的 LVGL 池里出。SD 读取 JSON 的缓冲（`RLOTTIE_SD_MAX_JSON_SIZE`，最大 48KB）也走 LVGL 池。这部分目前是够的。

注意 surface 的大小是按 `RLOTTIE_DEMO_SIZE` 的平方增长的：80→25KB，160→100KB，240→225KB。**放大动画尺寸时，先撑爆的是 LVGL 池，不是 C 库堆**，那会是另一个错误（`lv_rlottie_create_from_raw` 返回 NULL，而不是 bad_alloc），要分清。

## 本机 RAM 预算

从 `MDK-ARM/h7rec.uvprojx` 看，当前 RAM 映射：

```text
IRAM  (DTCM)     0x20000000  128KB   栈(16KB) + C库堆(64KB) + 部分 .bss/.data
IRAM2 (AXI SRAM) 0x24000000  512KB   大量空闲
```

问题就在这里：DTCM 只有 128KB，里面已经塞了 16KB 栈 + 64KB 堆 + 静态数据，**已经很挤**。直接把 `Heap_Size` 往上加，很容易和栈、静态数据撞车，链接时报 `L6406E: No space in execution regions`。

而 512KB 的 AXI SRAM（IRAM2）基本是空的。正确方向是**把 C 库堆挪到 AXI SRAM 并放大**，DTCM 留给栈和高速数据。

> 另外注意：AXI SRAM 是会被 DMA 访问的区域，rlottie 的堆放这里没有 DTCM 那种 DMA 访问限制问题（参见 `sdmmc_dma_dtcm_zh.md`），但反过来也要记得这块区域如果开了 DCache，DMA buffer 需要 cache 维护——堆本身只给 CPU 用，不涉及这个。

## 解决方案

### 方案一：快速验证（改一行，仅用于确认根因）

先确认是不是堆的问题。临时调大 `MDK-ARM/startup_stm32h743xx.s` 里的堆：

```asm
Heap_Size      EQU     0x18000      ; 64KB -> 96KB，先看还崩不崩
```

- 如果 DTCM 还能装下，崩溃消失或推迟，就坐实了“C 库堆不够”这个根因。
- 如果链接报 `L6406E: No space`，说明 DTCM 已经塞不下，直接上方案二。

这一步只是诊断手段，不建议作为最终配置——它把本就紧张的 DTCM 占得更满。

### 方案二：把 C 库堆放进 AXI SRAM（推荐）

加一个分散加载（scatter）文件，把 `ARM_LIB_HEAP` 指到 512KB 的 AXI SRAM，并给足空间（例如 256KB），DTCM 只留栈和高速数据。

思路示意（具体地址要按实际 .bss/.data 占用调整）：

```text
; DTCM：放栈 + 高频访问的小数据
RW_DTCM 0x20000000 0x00020000 {
    ...
    ARM_LIB_STACK 0x20000000 EMPTY 0x4000 {}   ; 16KB 栈
}

; AXI SRAM：放大块 .bss/.data 和 C 库堆
RW_AXI 0x24000000 0x00080000 {
    *(.bss .data ...)
    ARM_LIB_HEAP +0 EMPTY 0x40000 {}           ; 256KB 堆，给 rlottie 用
}
```

加 scatter 文件后要在 Keil 工程里 Options → Linker 勾选使用该 .sct，并取消 “Use Memory Layout from Target Dialog”。这一步会改动链接配置，属于中等风险改动，改完务必看 map 文件确认：

- `ARM_LIB_HEAP` 落在 `0x24......`（AXI SRAM）。
- 栈仍在 DTCM，且没有和堆/静态数据重叠。

### 方案三：从源头降低 rlottie 内存峰值（可叠加）

无论堆放哪，都可以顺手减小峰值：

- 简化动画：减少图层、关键帧、复杂 mask / trim path，解析出的模型树会小很多。
- 控制渲染尺寸 `RLOTTIE_DEMO_SIZE`，surface 占用是平方增长，过大同时压垮 LVGL 池。
- 一次只创建一个 rlottie 对象，用完 `lv_obj_del`，避免多个动画的堆峰值叠加。

## 如何确认根因（排查 checklist）

遇到 rlottie `bad_alloc` / SIGABRT 时，按顺序确认：

1. 确认报错来自 C 库堆，不是 LVGL 池：bad_alloc 一定是 `new`/`malloc`，与 `LV_MEM_SIZE` 无关。
2. 看 `startup_stm32h743xx.s` 里 `Heap_Size` 当前值（本工程是 0x10000 = 64KB）。
3. 看 map 文件里堆和栈的位置，确认是否都挤在 0x20000000 的 DTCM。
4. 临时按方案一加大堆，验证崩溃是否消失，坐实根因。
5. 若 DTCM 装不下，按方案二把堆迁到 0x24000000 的 AXI SRAM。
6. 检查 `RLOTTIE_DEMO_SIZE`，区分“surface 太大撑爆 LVGL 池（返回 NULL）”和“rlottie 解析撑爆 C 库堆（bad_alloc）”这两种不同问题。

## 结论

rlottie 的 `bad_alloc` / SIGABRT 根因是：

```text
rlottie 解析 + 栅格化的 new/malloc 走 C 库堆
C 库堆只有 64KB，且和栈、静态数据一起挤在 128KB 的 DTCM
解析/渲染峰值冲破 64KB
malloc 失败 -> bad_alloc -> 无异常环境下 abort -> SIGABRT
```

推荐做法是把 C 库堆迁到 512KB 的 AXI SRAM 并放大（方案二），DTCM 留给栈。改 `Heap_Size`（方案一）只适合快速验证，受 DTCM 容量限制。同时区分清楚 C 库堆与 LVGL 池：前者管 rlottie 内部分配，后者管渲染 surface，二者撑爆时的报错形态不同，不要张冠李戴。
