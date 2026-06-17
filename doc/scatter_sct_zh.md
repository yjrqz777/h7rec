# Keil scatter 文件说明

这份文档结合当前工程 `MDK-ARM/h7rec.sct` 来解释 scatter 文件怎么工作，以及为什么我们要这样放 DTCM 和 AXI。

## 1. scatter 是什么

`.sct` 是 Arm/Keil linker 的内存分配文件。
它不负责定义变量，只负责回答一件事：

> 某个 section 最后放到哪块 RAM 或 Flash。

一个典型结构是：

```sct
LR_IROM1 0x08000000 0x00200000 {
  ER_IROM1 0x08000000 0x00200000 {
    .ANY (+RO)
  }

  RW_DTCM 0x20000000 0x0001C000 {
    *(.dtcm_data*)
  }
}
```

## 2. 关键名词

- `LR` = Load Region，装载区
- `ER` = Execution Region，执行区
- `EMPTY` = 只预留空间，不放实际代码/数据
- `+RO` = 只读段
- `+RW` = 可读写段
- `+ZI` = 零初始化段
- `.ANY` = 自动收纳没有被显式指定的 section

## 3. 当前工程的 scatter

当前 `h7rec.sct` 大致是：

```sct
LR_IROM1 0x08000000 0x00200000  {
  ER_IROM1 0x08000000 0x00200000  {
    *.o (RESET, +First)
    *(InRoot$$Sections)
    .ANY (+RO)
    .ANY (+XO)
  }

  RW_DTCM 0x20000000 0x0001C000  {
    *(.dtcm_data*)
  }

  RW_DTCM_STACK 0x2001C000 0x00004000  {
    startup_stm32h743xx.o (STACK)
  }

  RW_AXI 0x24000000 0x00060000  {
    *(.noncacheable*)
    *(.axi_dma*)
    .ANY (+RW +ZI)
  }

  RW_AXI_HEAP 0x24060000 0x00020000  {
    startup_stm32h743xx.o (HEAP)
  }
}
```

## 4. 每一段在做什么

### Flash 区

```sct
ER_IROM1 0x08000000 0x00200000
```

放程序代码、只读常量、向量表。

### DTCM

```sct
RW_DTCM 0x20000000 0x0001C000
```

放你自己标记成 `*.dtcm_data*` 的数据。
这块 RAM CPU 访问快，但 DMA 不能直接访问。

### 启动栈

```sct
RW_DTCM_STACK 0x2001C000 0x00004000
startup_stm32h743xx.o (STACK)
```

这是 startup 文件里 `AREA STACK` 对应的 section。
我们把它显式放到 DTCM 顶部。

### AXI

```sct
RW_AXI 0x24000000 0x00060000
```

放普通全局变量、`ZI` 段、以及我们标记的 DMA 安全区：

- `.noncacheable*`
- `.axi_dma*`

这也是 `SDFatFS` 最终应该落到的地方。

### 启动堆

```sct
RW_AXI_HEAP 0x24060000 0x00020000
startup_stm32h743xx.o (HEAP)
```

这是 startup 文件里 `AREA HEAP` 对应的 section。
我们把它放在 AXI，避免 `malloc/new` 挤占 DTCM。

## 5. 为什么 `SDFatFS` 会被自动放到 AXI

`SDFatFS` 是 FatFs 里的一个全局对象，属于 `+RW +ZI` 类型。

因为它没有单独写进 `RW_DTCM`，所以会被：

```sct
.ANY (+RW +ZI)
```

自动收进去，最终进 `RW_AXI`。

这正是我们想要的结果。

## 6. 为什么不能让 SD 的 buffer 落到 DTCM

STM32H7 的 SDMMC DMA 不能访问 DTCM。

所以如果这些对象在 DTCM：

- `SDFatFS`
- `SDFatFS.win[]`
- 传给 FatFs 的读写 buffer
- `scratch` 之外的临时块

就可能触发 DMA 读写失败。

## 7. 为什么不能混用 `ARM_LIB_STACK` 和 startup 的 `STACK/HEAP`

startup 文件里本来就有：

- `AREA STACK`
- `AREA HEAP`

它们会生成：

- `startup_stm32h743xx.o(STACK)`
- `startup_stm32h743xx.o(HEAP)`

如果再额外写 `ARM_LIB_STACK`，就会让堆栈语义变得不统一。

当前工程选择的是：

- 用 startup 的 `STACK/HEAP`
- 在 scatter 里显式指定它们的位置

这样最直观，也最稳定。

## 8. 你可以怎么控制数据放哪

最常见有三种方式：

### 方式一：靠 section 名字

```c
__attribute__((section(".dtcm_data")))
static uint8_t buf[1024];
```

然后在 `.sct` 里写：

```sct
RW_DTCM {
  *(.dtcm_data*)
}
```

### 方式二：靠 `.ANY`

普通全局变量不写特殊 section，直接让 linker 自动分配。

### 方式三：显式指定某个对象文件的 section

像我们这里：

```sct
startup_stm32h743xx.o (STACK)
startup_stm32h743xx.o (HEAP)
```

## 9. 当前这版布局的目标

一句话就是：

- DTCM 给 CPU 热数据和栈
- AXI 给 FatFs、DMA、安全的全局数据、C 库 heap
- 不让 SDMMC DMA 去碰 DTCM

这能同时解决：

- SD 挂载时 DMA 报错
- `SDFatFS` 误落 DTCM
- `malloc` 在错误堆区里崩掉

