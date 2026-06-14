# 这是上游 rlottie 工程外面的一层小包装。
#
# 三个 CMake 入口文件的职责保持分离:
#   - CMakeLists.txt: 决定编译什么、强制哪些 rlottie 选项
#   - armclang-cortex-m7.cmake: 决定用哪个编译器、面向哪个目标芯片
#   - CMakePresets.json: 决定如何调用 CMake
cmake_minimum_required(VERSION 3.20)

# CMAKE_TOOLCHAIN_FILE 必须在 project() 启用 C/C++/ASM 之前生效。
# CMakePresets.json 会显式传入同一个工具链文件；这里保留 fallback，
# 这样直接执行 "cmake -S . -B build -G Ninja" 也能配置这个 Keil AC6 移植包。
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${CMAKE_CURRENT_LIST_DIR}/armclang-cortex-m7.cmake"
        CACHE FILEPATH "Toolchain file")
endif()

# CMake 会在这里加载工具链文件，并检测 armclang/armasm。
project(keil_ac6_rlottie LANGUAGES C CXX ASM)

# 给 Keil 工程使用时只需要静态库。
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# 上游或依赖项目可能使用 CTest 的标准 BUILD_TESTING 开关；裸机移植包不构建测试。
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

# 动态图片加载模块依赖 dlopen/dlsym，裸机目标上没有意义。
set(LOTTIE_MODULE OFF CACHE BOOL "" FORCE)

# Keil AC6 裸机 libc++ 没有线程支持。本移植包只走同步渲染，
# 少数上游无条件残留的 std::future/std::mutex 引用由 compat 头兜底。
set(LOTTIE_THREAD OFF CACHE BOOL "" FORCE)

# 关闭 rlottie 模型缓存，避免拉入带 mutex 的缓存路径。
set(LOTTIE_CACHE OFF CACHE BOOL "" FORCE)

# 测试和 example 都是桌面工具；example 还会使用当前 libc++ 不支持的线程 API。
set(LOTTIE_TEST OFF CACHE BOOL "" FORCE)
set(LOTTIE_EXAMPLE OFF CACHE BOOL "" FORCE)

# 上游默认安装到 /usr/lib，这对当前本地 Keil 包不合适。
set(LIB_INSTALL_DIR lib CACHE STRING "" FORCE)

# 把打过补丁的上游源码作为子工程构建。install 步骤会把头文件和
# rlottie.lib 复制到 External/keil_ac6_rlottie/install/。
add_subdirectory(rlottie-0.2)
