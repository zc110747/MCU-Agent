cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/arm-none-eabi-gcc.cmake CACHE FILEPATH "" FORCE)
project(stm32f429_touchpad C ASM)

# ------------------------------------------------------------------
# MCU / toolchain flags
# ------------------------------------------------------------------
# soft-float ABI, but allow FPU instructions: the FreeRTOS ARM_CM4F port
# contains unconditional vstmdbeq/vldmiaeq {s16-s31} which the assembler
# rejects with plain -mfloat-abi=soft.  The C code stays float-free.
set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -mfpu=fpv4-sp-d16")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # -> -std=gnu99 (allows // comments, GNU extensions)

# Zero warnings is a hard requirement.  Optimization level depends on build type:
# Debug keeps -Og (debug-friendly), Release uses -Os (MCU size-optimized).
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(OPT_LEVEL "-Os")
else()
  set(OPT_LEVEL "-Og")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MCU_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -fdata-sections -ffunction-sections -g ${OPT_LEVEL}")

set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${MCU_FLAGS} -x assembler-with-cpp -g -Og")

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/sys_startup/STM32F429IGTx_FLASH.ld)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MCU_FLAGS} -T${LINKER_SCRIPT} -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,--print-memory-usage -specs=nano.specs -lc -lm -lgcc")

# ------------------------------------------------------------------
# Include paths
# ------------------------------------------------------------------
include_directories(
  app
  bsp
  sys_startup
  Drivers/CMSIS/Include
  Drivers/STM32F4xx_HAL_Driver/Inc
  third_party/FreeRTOS-Kernel/include
  third_party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F
  third_party/Fatfs
  # LVGL: both the top dir (for lvgl.h -> "lv_conf.h") and src/ (for the
  # internal "#include ../lvgl.h" relative form used inside lvgl sources).
  third_party/lvgl
  third_party/lvgl/src
  # TinyUSB (device stack only).  app/tusb_config.h lives in app/.
  third_party/tinyusb/src
)

add_compile_definitions(
  STM32F429xx
  USE_HAL_DRIVER
  # TinyUSB: STM32F4 DWC2 core, FreeRTOS OSAL (tusb_config.h fills the rest).
  CFG_TUSB_MCU=OPT_MCU_STM32F4
  CFG_TUSB_OS=OPT_OS_FREERTOS
)

# ------------------------------------------------------------------
# Global logging switch (app/log.h)
# ------------------------------------------------------------------
#   PRINT_LOG_ENABLE=1 -> PRINT_LOG() routes to printf_log() -> uart_write()
#   PRINT_LOG_ENABLE=0 -> every PRINT_LOG() compiles to nothing
# Switch it off with:  cmake -S . -B build -DENABLE_PRINT_LOG=OFF
option(ENABLE_PRINT_LOG "Compile in PRINT_LOG() application logging" ON)
if(ENABLE_PRINT_LOG)
  add_compile_definitions(PRINT_LOG_ENABLE=1)
  message(STATUS "PRINT_LOG: enabled (PRINT_LOG_ENABLE=1)")
else()
  add_compile_definitions(PRINT_LOG_ENABLE=0)
  message(STATUS "PRINT_LOG: compiled out (PRINT_LOG_ENABLE=0)")
endif()

# ------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------
file(GLOB APP_SOURCES app/*.c)
file(GLOB BSP_SOURCES bsp/*.c)

set(CMSIS_SOURCES
  sys_startup/system_stm32f4xx.c
  sys_startup/gcc/startup_stm32f429xx.s
)

# Only the HAL modules we use (their HAL_*_MODULE_ENABLED are set in
# app/stm32f4xx_hal_conf.h, so each .c compiles cleanly).
set(HAL_SOURCES
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sdram.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sram.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_fmc.c
  # microSD over SDIO (4-bit, polled): HAL SD driver + low-level SDMMC engine
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sd.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_sdmmc.c
)

# FreeRTOS V11 kernel (native API): core + ARM_CM4F port + heap_5 (SDRAM).
set(FREERTOS_SOURCES
  third_party/FreeRTOS-Kernel/tasks.c
  third_party/FreeRTOS-Kernel/queue.c
  third_party/FreeRTOS-Kernel/list.c
  third_party/FreeRTOS-Kernel/timers.c
  third_party/FreeRTOS-Kernel/event_groups.c
  third_party/FreeRTOS-Kernel/stream_buffer.c
  third_party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c
  third_party/FreeRTOS-Kernel/portable/MemMang/heap_5.c
)

# ChaN FatFs R0.15.  diskio.c from the vendor tree is NOT compiled: the SDIO
# glue lives in app/fs_diskio.c (fs_lock serialization + aligned bounce buffer).
set(FATFS_SOURCES
  third_party/Fatfs/ff.c
  third_party/Fatfs/ffunicode.c
  third_party/Fatfs/ffsystem.c
)

# LVGL v8 (recursive: every .c under src/, includes the FatFs fs driver that
# lv_conf.h enables with LV_USE_FS_FATFS).
file(GLOB_RECURSE LVGL_SOURCES third_party/lvgl/src/*.c)

# TinyUSB device stack + HID class + the STM32 DWC2 port + FreeRTOS OSAL.
# The host stack, MSC/CDC/Audio classes and the other MCU ports are NOT built:
# this project only ever presents itself as a HID mouse.
set(TINYUSB_SOURCES
  third_party/tinyusb/src/tusb.c
  third_party/tinyusb/src/common/tusb_fifo.c
  third_party/tinyusb/src/device/usbd.c
  third_party/tinyusb/src/class/hid/hid_device.c
  third_party/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c
  third_party/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c
)

# Third-party library: keep its warnings out of the build so the project's own
# code stays strictly warning-clean under -Wall -Wextra.
set_source_files_properties(${LVGL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(${FATFS_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(${TINYUSB_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

# ------------------------------------------------------------------
# Executable
# ------------------------------------------------------------------
set(CMAKE_EXECUTABLE_SUFFIX .elf)

add_executable(${PROJECT_NAME}.elf
  ${APP_SOURCES}
  ${BSP_SOURCES}
  ${CMSIS_SOURCES}
  ${HAL_SOURCES}
  ${FREERTOS_SOURCES}
  ${FATFS_SOURCES}
  ${LVGL_SOURCES}
  ${TINYUSB_SOURCES}
)

# FreeRTOS ARM_CM4F port has unconditional vstmdbeq/vldmiaeq {s16-s31} in
# its naked PendSV handler.  With -mfloat-abi=soft the assembler disables
# the FPU instruction set, so compile just this file with softfp.
set_source_files_properties(
  third_party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c
  PROPERTIES COMPILE_OPTIONS "-mfloat-abi=softfp;-mfpu=fpv4-sp-d16"
)

set_target_properties(${PROJECT_NAME}.elf PROPERTIES
  OUTPUT_NAME ${PROJECT_NAME}
  LINK_DEPENDS ${LINKER_SCRIPT}
)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
  COMMAND ${CMAKE_OBJCOPY} -O ihex  $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
  COMMENT "Generating ${PROJECT_NAME}.hex / ${PROJECT_NAME}.bin"
)
