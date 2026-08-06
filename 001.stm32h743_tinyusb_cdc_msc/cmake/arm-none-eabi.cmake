# ---------------------------------------------------------------------------
# CMake toolchain file: arm-none-eabi-gcc (bare-metal Cortex-M7)
# ---------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Do not try to link a full executable when probing the compiler:
# bare-metal toolchains have no default startup/linker script.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(WIN32)
  set(TOOLCHAIN_SUFFIX ".exe")
else()
  set(TOOLCHAIN_SUFFIX "")
endif()

# TOOLCHAIN_PREFIX may be passed in with -DTOOLCHAIN_PREFIX=/path/to/bin/
set(TOOLCHAIN_PREFIX "" CACHE STRING "Directory containing arm-none-eabi-* (with trailing slash)")

set(CMAKE_C_COMPILER   "${TOOLCHAIN_PREFIX}arm-none-eabi-gcc${TOOLCHAIN_SUFFIX}")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_PREFIX}arm-none-eabi-gcc${TOOLCHAIN_SUFFIX}")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_PREFIX}arm-none-eabi-g++${TOOLCHAIN_SUFFIX}")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_PREFIX}arm-none-eabi-objcopy${TOOLCHAIN_SUFFIX}" CACHE INTERNAL "")
set(CMAKE_SIZE         "${TOOLCHAIN_PREFIX}arm-none-eabi-size${TOOLCHAIN_SUFFIX}"    CACHE INTERNAL "")
set(CMAKE_OBJDUMP      "${TOOLCHAIN_PREFIX}arm-none-eabi-objdump${TOOLCHAIN_SUFFIX}" CACHE INTERNAL "")

# Cortex-M7 with double-precision FPU
set(MCU_FLAGS
  -mcpu=cortex-m7
  -mthumb
  -mfpu=fpv5-d16
  -mfloat-abi=hard
)
string(REPLACE ";" " " MCU_FLAGS_STR "${MCU_FLAGS}")

set(CMAKE_C_FLAGS_INIT   "${MCU_FLAGS_STR}")
set(CMAKE_CXX_FLAGS_INIT "${MCU_FLAGS_STR}")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS_STR} -x assembler-with-cpp")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
