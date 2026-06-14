# Toolchain file for building rlottie with Keil Arm Compiler 6 for STM32H743
# / Cortex-M7. CMake loads this file before project() enables any language.
#
# Keep target/compiler details here instead of in CMakeLists.txt so the wrapper
# project stays readable and CMake can initialize the cross compiler correctly.

# Bare-metal target: there is no host OS such as Windows/Linux on the MCU.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m7)

# During compiler checks CMake normally tries to link an executable. A bare-metal
# executable would need startup files and a linker script, so ask CMake to only
# try compiling static libraries for its probes.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ARMCLANG_BIN can be passed from CMakePresets.json, an environment variable, or
# falls back to the default Keil installation path used on this machine.
if(NOT DEFINED ARMCLANG_BIN)
    if(DEFINED ENV{ARMCLANG_BIN})
        set(ARMCLANG_BIN "$ENV{ARMCLANG_BIN}")
    else()
        set(ARMCLANG_BIN "D:/App/Keil/Keil_v5/ARM/ARMCLANG/bin")
    endif()
endif()

# Normalize Windows backslashes so CMake command lines are stable.
file(TO_CMAKE_PATH "${ARMCLANG_BIN}" ARMCLANG_BIN)

# Use Keil's AC6 tools for C, C++, assembler, and static library archiving.
set(CMAKE_C_COMPILER "${ARMCLANG_BIN}/armclang.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${ARMCLANG_BIN}/armclang.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER "${ARMCLANG_BIN}/armasm.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${ARMCLANG_BIN}/armar.exe" CACHE FILEPATH "" FORCE)

# Make the produced static library look like a Keil library: rlottie.lib instead
# of librlottie.a.
set(CMAKE_STATIC_LIBRARY_PREFIX "")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".lib")

# CPU/ABI flags must match the STM32H743 Keil project, otherwise the static
# library can fail to link or use the wrong floating-point ABI.
set(ARM_CPU_FLAGS "--target=arm-arm-none-eabi -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
set(ARM_ABI_FLAGS "-fshort-enums -fshort-wchar")
set(ARM_SIZE_FLAGS "-ffunction-sections -fdata-sections")

# Compatibility headers replace unavailable desktop/POSIX pieces such as
# dlfcn.h, std::future, std::mutex, and std::condition_variable.
set(RLOTTIE_COMPAT_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/compat/include")

# Used by the patched rlottie vector CMakeLists to skip vdebug.cpp, which pulls
# in std::thread unconditionally when logging is enabled.
set(AC6_RLOTTIE_NO_THREAD_LIBCXX ON CACHE BOOL "Build rlottie with AC6 libc++ without thread headers" FORCE)

# C sources only need the target flags and compatibility include path.
set(CMAKE_C_FLAGS
    "${ARM_CPU_FLAGS} ${ARM_ABI_FLAGS} ${ARM_SIZE_FLAGS} -I${RLOTTIE_COMPAT_INCLUDE}"
    CACHE STRING "" FORCE)

# C++ sources additionally disable features that are expensive or unavailable in
# this embedded build, and force-include the rlottie compatibility header into
# every translation unit.
set(CMAKE_CXX_FLAGS
    "${ARM_CPU_FLAGS} ${ARM_ABI_FLAGS} ${ARM_SIZE_FLAGS} -I${RLOTTIE_COMPAT_INCLUDE} -include rlottie_armclang_compat.h -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics"
    CACHE STRING "" FORCE)

# CMake's default static-library rule for this platform may try Unix-like ar
# arguments. Keil's armar uses --create, so override the archive rules.
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> --create <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_C_ARCHIVE_APPEND "<CMAKE_AR> --create <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_C_ARCHIVE_FINISH "")
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> --create <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> --create <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_FINISH "")

