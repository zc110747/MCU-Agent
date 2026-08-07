# ---------------------------------------------------------------------------
#  Bare-metal ARM Cortex-M7 toolchain file (arm-none-eabi-gcc)
#
#  The compiler is expected to be on PATH - no absolute paths are hard coded,
#  so the project stays portable across machines.
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  arm)

# Skip the compiler ABI check: linking a full test binary needs the linker
# script and startup code, which are not available at configure time.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(WIN32)
    set(TOOLCHAIN_SUFFIX ".exe")
else()
    set(TOOLCHAIN_SUFFIX "")
endif()

set(TOOLCHAIN_PREFIX arm-none-eabi-)

set(CMAKE_C_COMPILER    ${TOOLCHAIN_PREFIX}gcc${TOOLCHAIN_SUFFIX})
set(CMAKE_ASM_COMPILER  ${TOOLCHAIN_PREFIX}gcc${TOOLCHAIN_SUFFIX})
set(CMAKE_CXX_COMPILER  ${TOOLCHAIN_PREFIX}g++${TOOLCHAIN_SUFFIX})
set(CMAKE_OBJCOPY       ${TOOLCHAIN_PREFIX}objcopy${TOOLCHAIN_SUFFIX} CACHE INTERNAL "")
set(CMAKE_OBJDUMP       ${TOOLCHAIN_PREFIX}objdump${TOOLCHAIN_SUFFIX} CACHE INTERNAL "")
set(CMAKE_SIZE          ${TOOLCHAIN_PREFIX}size${TOOLCHAIN_SUFFIX}    CACHE INTERNAL "")

# Cortex-M7 with double precision FPU
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
