# Keil AC6 rlottie 编译指南

本目录用于把 `rlottie-0.2` 交叉编译成 Keil/Arm Compiler 6 可链接的静态库：

```text
External/keil_ac6_rlottie/install/lib/rlottie.lib
```

生成的 `rlottie.lib` 供 STM32H743 Keil 工程和 LVGL `lv_rlottie` 使用。

## 目录说明

```text
External/keil_ac6_rlottie/
  CMakeLists.txt                 # 外层 CMake wrapper，配置 rlottie 构建选项
  CMakePresets.json              # CMake preset，固定 build/install/toolchain 参数
  armclang-cortex-m7.cmake        # Keil AC6 + Cortex-M7 工具链文件
  build.bat                      # Windows 一键构建入口
  build_rlottie_armclang.ps1      # 旧 PowerShell 构建入口，保留兼容
  rlottie-0.2/                    # 上游 rlottie 源码
  compat/include/                 # AC6 裸机 libc++ 兼容头
  keil/keil_ac6_retarget.c        # Keil/AC6 no-semihosting + RTT retarget
  build/                          # CMake 构建目录，自动生成
  install/                        # 安装目录，Keil 工程引用这里
```

## 前置条件

需要安装并能在命令行找到：

```text
CMake
Ninja
Keil MDK / Arm Compiler 6
```

默认 Keil AC6 工具链路径在 `CMakePresets.json` 中配置为：

```text
D:/App/Keil/Keil_v5/ARM/ARMCLANG/bin
```

如果你的 Keil 安装路径不同，修改：

```json
"ARMCLANG_BIN": "D:/App/Keil/Keil_v5/ARM/ARMCLANG/bin"
```

或者在命令行额外传入：

```powershell
cmake --preset keil-ac6-m7 -DARMCLANG_BIN=D:/your/Keil/ARM/ARMCLANG/bin
```

## 编译方法

推荐使用 CMake preset：

```powershell
cd External\keil_ac6_rlottie
cmake --preset keil-ac6-m7
cmake --build --preset keil-ac6-m7
cmake --install build
```

也可以直接运行：

```powershell
cd External\keil_ac6_rlottie
.\build.bat
```

`build.bat` 内部执行的就是上面三条 CMake 命令。

## 三个 CMake 步骤的含义

```powershell
cmake --preset keil-ac6-m7
```

配置并生成 Ninja 构建文件，读取：

```text
CMakeLists.txt
CMakePresets.json
armclang-cortex-m7.cmake
```

```powershell
cmake --build --preset keil-ac6-m7
```

调用 `armclang.exe` / `armar.exe` 编译 rlottie，生成静态库。

```powershell
cmake --install build
```

把库和头文件复制到 `install/`，供 Keil 工程引用。

## 编译产物

成功后应生成：

```text
External/keil_ac6_rlottie/install/lib/rlottie.lib
External/keil_ac6_rlottie/install/include/rlottie.h
External/keil_ac6_rlottie/install/include/rlottie_capi.h
External/keil_ac6_rlottie/install/include/rlottiecommon.h
```

Keil 工程只需要引用 `install/` 下的产物，不需要引用 `build/`。

## 清理重编

如果修改了工具链路径、CMakeLists 或遇到 source directory 不匹配错误，删除 `build/` 后重新配置：

```powershell
cd External\keil_ac6_rlottie
Remove-Item .\build -Recurse -Force
cmake --preset keil-ac6-m7
cmake --build --preset keil-ac6-m7
cmake --install build
```

`install/` 可以保留；重新 install 会覆盖其中的库和 CMake 文件。

## Keil 工程引用

Keil 工程需要配置：

| 配置项 | 路径 / 值 |
| --- | --- |
| C/C++ Include Paths | `../External/keil_ac6_rlottie/install/include` |
| Library Search Path | `../External/keil_ac6_rlottie/install/lib` |
| Linker Input File | `../External/keil_ac6_rlottie/install/lib/rlottie.lib` |
| Retarget 源文件 | `../External/keil_ac6_rlottie/keil/keil_ac6_retarget.c` |
| LVGL 配置 | `LV_USE_RLOTTIE 1` |

`keil_ac6_retarget.c` 用于禁用 semihosting，并把标准输出重定向到 SEGGER RTT。链接 rlottie 后 C/C++ 运行库可能引用 `_sys_*`，所以这个文件需要参与 Keil 工程编译。

## 关键文件

### `CMakeLists.txt`

外层 wrapper，负责设置 rlottie 选项：

```text
BUILD_SHARED_LIBS=OFF
BUILD_TESTING=OFF
LOTTIE_MODULE=OFF
LOTTIE_THREAD=OFF
LOTTIE_CACHE=OFF
LOTTIE_TEST=OFF
LOTTIE_EXAMPLE=OFF
LIB_INSTALL_DIR=lib
```

### `armclang-cortex-m7.cmake`

工具链文件，负责告诉 CMake 使用 Keil AC6：

```text
armclang.exe
armasm.exe
armar.exe
Cortex-M7
FPv5-D16
hard-float ABI
```

并强制包含 `compat/include/rlottie_armclang_compat.h`。

### `CMakePresets.json`

固定 CMake 调用参数：

```text
generator = Ninja
binaryDir = build
toolchainFile = armclang-cortex-m7.cmake
install prefix = install
```

## 常见问题

### CMake 提示 source directory 不匹配

通常是旧 `build/` 目录曾经用别的 `-S` 路径配置过。删除 `build/` 后重新执行 preset。

### 找不到 `armclang.exe`

检查 `CMakePresets.json` 中的 `ARMCLANG_BIN` 是否等于你的 Keil AC6 安装路径。

### 生成了库但 Keil 链接找不到

确认 Keil 工程引用的是：

```text
External/keil_ac6_rlottie/install/lib/rlottie.lib
```

不是 `build/` 目录下的临时产物。

### 链接出现 `_sys_*` 或 semihosting 相关错误

确认 Keil 工程加入了：

```text
External/keil_ac6_rlottie/keil/keil_ac6_retarget.c
```

同时不要再引入会重复定义 `_sys_*` 的其他 retarget/syscalls 文件。
