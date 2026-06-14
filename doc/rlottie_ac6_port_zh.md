# AC6 rlottie 移植说明

用 **Keil / Arm Compiler 6 (ArmClang)** 把 `External/keil_ac6_rlottie/rlottie-0.2` 交叉编译成
静态库 `rlottie.lib`，并链接进 STM32H743 的 Keil(MDK-ARM, AC6)工程，让 LVGL
能在 ST7735 上播放 Lottie 矢量动画。

rlottie 原本是为桌面平台(带操作系统、带线程、带异常的完整 C++ 运行时)设计的，
而 ArmClang 的裸机 libc++ 是 **无线程、无异常、无 RTTI** 配置，目标芯片也没有
动态加载和文件系统语义。移植的工作量基本都花在“抹平这层运行时差异”上。

> 本文为移植过程记录与排错笔记。移植产物与构建脚本见
> `External/keil_ac6_rlottie/`，该目录另有一份同内容的 `README.md`。

---

## 目录结构

```text
External/keil_ac6_rlottie/
  rlottie-0.2/                 # 上游 rlottie 源码
  CMakeLists.txt               # Keil/AC6 wrapper, calls rlottie-0.2
  CMakePresets.json            # CMake configure/build preset
  armclang-cortex-m7.cmake     # CMake 交叉编译工具链文件
  build_rlottie_armclang.ps1   # legacy PowerShell helper
  compat/include/              # ArmClang 无线程 libc++ 的兼容 shim
    rlottie_armclang_compat.h  # 强制 include 进每个 TU 的兼容头
    mutex                      # std::mutex / lock_guard / unique_lock 空实现
    condition_variable         # std::condition_variable 空实现
    future                     # std::future / promise 最小 stub
    dlfcn.h                    # dlopen/dlsym 占位头(实际不会被调用)
  keil/keil_ac6_retarget.c     # Keil/AC6 工程侧的 _sys_* 重定向(去半主机)
  build/                       # 脚本生成
  install/                     # 脚本生成，供 Keil 引用
```

## 编译方法

```powershell
cd External\keil_ac6_rlottie
cmake --preset keil-ac6-m7
cmake --build --preset keil-ac6-m7
cmake --install build
```

脚本依赖:CMake(>=3.x，脚本里加了策略兼容)、Ninja、以及 Keil 自带的
`armclang / armasm / armar`(位于 `D:/App/Keil/Keil_v5/ARM/ARMCLANG/bin/`)。
产物为 `External/keil_ac6_rlottie/install/lib/rlottie.lib` 和 `install/include/`。

## Keil 工程侧配置

| 配置项 | 值 |
| --- | --- |
| C/C++ Include Paths | `External/keil_ac6_rlottie/install/include` |
| Linker Misc / Libs | `External/keil_ac6_rlottie/install/lib/rlottie.lib` |
| 源文件加入工程 | `External/keil_ac6_rlottie/keil/keil_ac6_retarget.c` |
| `lv_conf.h` | `LV_USE_RLOTTIE 1` |
| 启动文件堆 | `Heap_Size EQU 0x10000`(64KB，供 rlottie 的 C++ 堆) |

---

## 移植中遇到的问题与解决

### 1. 工具链 / CMake 配置类

| 现象 | 原因 | 解决 |
| --- | --- | --- |
| `CMake 3.5 以下被移除` 配置报错 | 上游 `CMakeLists.txt` 声明的最低版本太老，新版 CMake 拒绝 | 配置时加 `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` |
| 找不到工具链文件 | `-DCMAKE_TOOLCHAIN_FILE` 用了相对路径 | 改用 `.cmake` 文件的绝对路径 |
| `CMAKE_SYSTEM_PROCESSOR` 报错(CMP0123) | 工具链里写的是 `arm`，新策略要求具体处理器名 | 改成 `cortex-m7` |
| try-compile 失败 | 默认按可执行文件试编译，裸机无 startup/链接脚本 | `CMAKE_TRY_COMPILE_TARGET_TYPE = STATIC_LIBRARY` |

工具链关键编译选项:
`--target=arm-arm-none-eabi -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16
-mfloat-abi=hard -fshort-enums -fshort-wchar -ffunction-sections
-fdata-sections`，C++ 额外加 `-fno-exceptions -fno-rtti -fno-use-cxa-atexit`，
汇编 `--cpu=cortex-m7.fp.dp`。并对每个 TU 强制
`-include rlottie_armclang_compat.h`。

### 2. 无线程 libc++ 类(核心难点)

ArmClang 的 libc++ 以 `_LIBCPP_HAS_NO_THREADS` 编译，**`std::mutex`、
`std::lock_guard`、`std::unique_lock`、`std::condition_variable`、
`std::thread`、`std::future` 这些类型根本不存在**，包含 `<mutex>`、`<thread>`、
`<future>` 会直接报 “not supported since libc++ has been configured without
support for threads”。

- rlottie 关闭了线程(`LOTTIE_THREAD=OF`)后，大部分线程代码会被宏屏蔽，但
  仍有 **无条件的残留**:`lottieanimation.cpp` 顶部无条件出现
  `std::future<Surface> receiver;`(线程开关的 `#if` 在它后面),把 `<future>`
  拖进编译。
- 解决方式:在 `compat/include/` 提供一组 **最小 no-op shim**,并通过
  `rlottie_armclang_compat.h` 强制 include。shim 在检测到
  `_LIBCPP_HAS_NO_THREADS` 时,往 `namespace std` 注入:
  - `mutex`(`lock/try_lock/unlock` 全空操作)
  - `lock_guard`、`unique_lock`(含 `defer_lock`/`adopt_lock` 标签)
  - `condition_variable`(`notify_*`/`wait*` 空操作)、`cv_status` 枚举
  - `future`/`promise` 的最小占位类型
  这样既不真正引入线程,又能让模板实例化通过。

> 关键判断:rlottie 单线程渲染本就走同步路径,这些同步原语在“无线程”下退化
> 为无操作是安全的,不会改变渲染结果。

### 3. 动态加载 `dlfcn.h`

- `vimageloader.cpp` 里 `#ifdef _WIN32 ... #else #include <dlfcn.h>`,在裸机上
  `dlfcn.h` 不存在,直接 `fatal error: 'dlfcn.h' file not found`。
- 但 `dlopen` 的真实调用都在 `LOTTIE_IMAGE_MODULE_SUPPORT` 之后(该开关关闭),
  也就是说头被包含、但函数永远不会被调用。
- 解决:在 `compat/include/` 放一个 **占位 `dlfcn.h`**,提供 `dlopen/dlsym/
  dlclose` 的空声明,让包含通过即可。

### 4. 去半主机 / 链接期问题(工程侧)

把 `rlottie.lib` 链进 Keil 后,出现一连串链接和运行期问题:

| 现象 | 原因 | 解决 |
| --- | --- | --- |
| `L6218E: Undefined symbol lottie_animation_from_data` 等 | `lv_rlottie.o` 引用了 rlottie 的 C API,但库没进链接 | Linker 加 `rlottie.lib` 作为 `LinkerInputFile` |
| 上电即 HardFault,PC 落在 `_sys_open` | C++ 运行时初始化触发半主机 `_sys_*`,芯片无调试器时 `BKPT` 触发 `DEBUGEVT`/HardFault | 增加去半主机重定向(见下) |
| `L6915E: __use_no_semihosting requested, but _sys_exit/_sys_open/_ttywrch referenced` | 只声明 `__use_no_semihosting` 但没给全 `_sys_*` 实现 | `keil_ac6_retarget.c` **无条件**实现所有 `_sys_*` |
| `L6200E: _sys_seek multiply defined` | 同时引入了 SEGGER 的 `SEGGER_RTT_Syscalls_KEIL.c`,与自定义实现重复 | 移除 SEGGER 那个文件,只保留自定义 `keil_ac6_retarget.c` |
| `'#pragma import' is an Arm Compiler 5 extension` | `#pragma import(__use_no_semihosting)` 是 AC5 写法 | AC6 改用 `__asm(".global __use_no_semihosting");` |
| `L6218E: Undefined symbol HardFault_HandlerC` | 故障处理辅助函数被声明为 `static` | 去掉 `static`,改为外部可见 |

`keil/keil_ac6_retarget.c` 的核心:声明 `__use_no_semihosting`,并把
`_sys_write`/`_sys_open`/`_sys_read`/`_sys_close`/`_sys_seek`/`_sys_istty`/
`_sys_flen`/`_sys_exit`/`_ttywrch` 等全部给出实现,其中 `_sys_write` 把输出
重定向到 SEGGER RTT。**所有 `_sys_*` 必须无条件提供**,因为 SEGGER 自带的
KEIL syscalls 用 `#if __ARMCC_VERSION <= 6000000` 把实现挡在 AC6.22 之外,导致
linker 报 L6915E。

> `__ARMCC_VERSION` 这个宏 AC5、AC6 都会定义(AC6.22 = `6220000`),无法靠它区分
> 两者;真正能区分的是 `__clang__`(只有 AC6 定义)。因此 `keil_ac6_retarget.c`
> 不必用 `#if defined(__ARMCC_VERSION)` 做防御,直接无条件实现即可。

### 5. 运行期资源类

| 项 | 调整 |
| --- | --- |
| 堆 | 启动文件 `Heap_Size` 从 `0x2000` 提到 `0x10000`(64KB),rlottie 的 C++ 解析/渲染走堆分配 |
| 栈 | `Stack_Size = 0x4000` |
| LVGL 任务 | `uiTask` 栈 `1024*6`,运行 `lv_task_handler()` 循环 |
| 调试 | HardFault 处理改为 naked,跳转到非 static 的 `HardFault_HandlerC`,打印栈帧 PC/LR/寄存器及 CFSR/HFSR/DFSR 译码,便于定位半主机 BKPT |

---

## 经验小结

1. **裸机移植桌面 C++ 库,90% 的坑在“无线程 libc++”**:不是用宏关掉线程就够了,
   要处理源码里无条件残留的 `<future>`/`<thread>`/`<mutex>` 包含,最省事的办法是
   提供 no-op 的 std shim,而不是逐处 patch 上游源码。
2. **去半主机要彻底**:声明 `__use_no_semihosting` 后必须把所有被引用的 `_sys_*`
   补齐,否则 L6915E;且不能和 SEGGER 自带 syscalls 共存(L6200E)。
3. **AC5/AC6 语法差异**:`#pragma import` → `__asm(".global ...")`;不要用
   `__ARMCC_VERSION` 区分 AC5/AC6,要用 `__clang__`。
4. **优先用强制 include 的兼容头 + 占位头,而非改上游源码**:升级 rlottie 时
   兼容层不动即可,改动面最小。
