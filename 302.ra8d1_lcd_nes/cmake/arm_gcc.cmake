# arm_gcc.cmake - toolchain file for Renesas RA8D1 (Cortex-M85), Arm GNU GCC
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_AR arm-none-eabi-ar)
set(CMAKE_RANLIB arm-none-eabi-ranlib)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP arm-none-eabi-objdump)
set(CMAKE_SIZE arm-none-eabi-size)

# Cortex-M85 r0p2, Helium/MVE + DP FPU; flags verified by probe (Phase 0.5)
set(MCU_FLAGS "-mcpu=cortex-m85 -mfpu=auto -mfloat-abi=hard -mthumb")

set(CMAKE_C_FLAGS_INIT "${MCU_FLAGS} -std=gnu11")
set(CMAKE_CXX_FLAGS_INIT "${MCU_FLAGS} -std=gnu++17")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS}")
