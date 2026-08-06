# =============================================================================
#  Toolchain file for ARM Cortex-M (arm-none-eabi-gcc)
#
#  The compiler is located through the system PATH (the user already added
#  arm-none-eabi-* to PATH), so this file contains NO absolute paths and the
#  project is portable across machines.
#
#  Used by CMakeLists.txt via:
#      -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake
# =============================================================================

set(CMAKE_SYSTEM_NAME    Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# --- Compilers / tools (resolved via PATH) -----------------------------------
set(CMAKE_C_COMPILER     arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER   arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER   arm-none-eabi-gcc)

set(CMAKE_OBJCOPY        arm-none-eabi-objcopy  CACHE STRING "" FORCE)
set(CMAKE_OBJDUMP        arm-none-eabi-objdump  CACHE STRING "" FORCE)
set(CMAKE_SIZE           arm-none-eabi-size     CACHE STRING "" FORCE)
set(CMAKE_GDB            arm-none-eabi-gdb      CACHE STRING "" FORCE)
set(CMAKE_DEBUGGER       arm-none-eabi-gdb      CACHE STRING "" FORCE)

# Cross-compiling: do not run the compiler to probe the host environment.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Search strategy: programs from host PATH, libs/headers from the sysroot only.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_SYSTEM  ONLY)
