# 从零编译 rlottie

本文说明如何在 Windows + Keil MDK / Arm Compiler 6 环境下，从零把 `rlottie-0.2` 编译成 STM32H743 Keil 工程可链接的静态库。

目标产物：

```text
External/keil_ac6_rlottie/install/lib/rlottie.lib
External/keil_ac6_rlottie/install/include/rlottie.h
External/keil_ac6_rlottie/install/include/rlottie_capi.h
External/keil_ac6_rlottie/install/include/rlottiecommon.h
```

## 1. 准备工具

需要安装：

```text
Keil MDK / Arm Compiler 6
CMake
Ninja
```

确认 Keil AC6 工具链目录存在，例如：

```text
D:/App/Keil/Keil_v5/ARM/ARMCLANG/bin
```

该目录下应能看到：

```text
armclang.exe
armasm.exe
armar.exe
```

如果你的路径不同，后面需要修改 `CMakePresets.json` 里的 `ARMCLANG_BIN`。

## 2. 准备源码目录

目录应类似：

```text
External/keil_ac6_rlottie/
  CMakeLists.txt
  CMakePresets.json
  armclang-cortex-m7.cmake
  rlottie-0.2/
  compat/include/
  keil/keil_ac6_retarget.c
```

其中 `rlottie-0.2/` 是上游 rlottie 源码目录。本移植包已经对上游 CMake 做了少量 Keil AC6 适配：

```text
输出 rlottie.lib
允许关闭 example
跳过会拉入 std::thread 的 vdebug.cpp
修正外层 add_subdirectory 时 rlottie.expmap 的路径
```

## 3. 检查工具链路径

打开：

```text
External/keil_ac6_rlottie/CMakePresets.json
```

确认：

```json
"ARMCLANG_BIN": "D:/App/Keil/Keil_v5/ARM/ARMCLANG/bin"
```

如果 Keil 不在这个目录，改成你的实际路径。

也可以不改文件，配置时临时覆盖：

```powershell
cmake --preset keil-ac6-m7 -DARMCLANG_BIN=D:/your/Keil/ARM/ARMCLANG/bin
```

## 4. 配置 CMake

进入目录：

```powershell
cd External\keil_ac6_rlottie
```

执行配置：

```powershell
cmake --preset keil-ac6-m7
```

这一步会：

```text
读取 CMakeLists.txt
读取 CMakePresets.json
加载 armclang-cortex-m7.cmake
检测 armclang/armasm/armar
生成 build/build.ninja
```

这一步不会真正编译源码。

## 5. 编译 rlottie

执行：

```powershell
cmake --build --preset keil-ac6-m7
```

这一步会调用 Keil AC6 工具链编译 rlottie，并生成临时构建产物：

```text
External/keil_ac6_rlottie/build/rlottie-0.2/rlottie.lib
```

Keil 工程不建议直接引用 `build/` 下的库，因为它是构建目录产物。

## 6. 安装产物

执行：

```powershell
cmake --install build
```

这一步会把头文件和库复制到稳定的安装目录：

```text
External/keil_ac6_rlottie/install/
```

最终 Keil 工程应引用 `install/` 下的文件。

## 7. 一键构建

如果不想手动执行三条命令，可以运行：

```powershell
cd External\keil_ac6_rlottie
.\build.bat
```

`build.bat` 内容等价于：

```bat
cmake --preset keil-ac6-m7
cmake --build --preset keil-ac6-m7
cmake --install build
```

## 8. 验证结果

确认文件存在：

```text
External/keil_ac6_rlottie/install/lib/rlottie.lib
External/keil_ac6_rlottie/install/include/rlottie.h
External/keil_ac6_rlottie/install/include/rlottie_capi.h
External/keil_ac6_rlottie/install/include/rlottiecommon.h
```

如果 `rlottie.lib` 不存在，说明 build 或 install 阶段失败，需要回看命令行中的第一个 error。

## 9. 接入 Keil 工程

Keil 工程需要配置：

| 项目 | 路径 / 值 |
| --- | --- |
| Include Path | `../External/keil_ac6_rlottie/install/include` |
| Library Path | `../External/keil_ac6_rlottie/install/lib` |
| Linker Input | `../External/keil_ac6_rlottie/install/lib/rlottie.lib` |
| Retarget Source | `../External/keil_ac6_rlottie/keil/keil_ac6_retarget.c` |
| LVGL Config | `LV_USE_RLOTTIE 1` |

`keil_ac6_retarget.c` 需要参与 Keil 工程编译。它用于禁用 semihosting，并提供 `_sys_*` / `_ttywrch` 等 AC6 C library 需要的底层函数。

## 10. 常见问题

### source directory 不匹配

如果出现类似：

```text
The source ".../CMakeLists.txt" does not match the source ".../rlottie-0.2/CMakeLists.txt" used to generate cache
```

说明旧的 `build/` 目录是用另一种 `-S` 路径生成的。删除 `build/` 后重新配置：

```powershell
Remove-Item .\build -Recurse -Force
cmake --preset keil-ac6-m7
```

### 找不到 armclang.exe

检查 `CMakePresets.json` 中的 `ARMCLANG_BIN` 是否正确。

### CMake 版本提示兼容性问题

本 preset 已设置：

```json
"CMAKE_POLICY_VERSION_MINIMUM": "3.5"
```

用于兼容 rlottie 上游较老的 `cmake_minimum_required`。

### 编译时遇到 std::future / std::mutex / dlfcn.h

本移植包通过 `compat/include/` 提供兼容头，并在工具链文件里强制 include：

```text
compat/include/rlottie_armclang_compat.h
```

正常使用 preset 构建时不需要额外处理。

### Keil 链接 `_sys_*` 重复定义

不要同时加入多个 retarget/syscalls 文件。通常保留：

```text
External/keil_ac6_rlottie/keil/keil_ac6_retarget.c
```

并移除其他可能定义 `_sys_open`、`_sys_write`、`_sys_seek` 的文件。

