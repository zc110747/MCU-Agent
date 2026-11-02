cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/arm-none-eabi-gcc.cmake CACHE FILEPATH "" FORCE)
project(stm32f429_video C ASM)

# ------------------------------------------------------------------
# MCU / toolchain flags
# ------------------------------------------------------------------
# soft-float ABI, but allow FPU instructions: the FreeRTOS ARM_CM4F port
# contains unconditional vstmdbeq/vldmiaeq {s16-s31} which the assembler
# rejects with plain -mfloat-abi=soft.  The C code stays float-free.
set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -mfpu=fpv4-sp-d16")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # -> -std=gnu99

# Zero warnings is a hard requirement.  Debug keeps -Og, Release uses -Os.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(OPT_LEVEL "-Os")
else()
  set(OPT_LEVEL "-Og")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MCU_FLAGS} -Wall -Wextra -Wno-unused-parameter -fdata-sections -ffunction-sections -g ${OPT_LEVEL}")

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
  # libjpeg decompressor (row sink patched to app/mjpeg_row.h)
  third_party/jpeg
  # LVGL: the top dir resolves "lv_conf.h", src/ resolves the internal
  # "../lvgl.h" form used inside the library.
  third_party/lvgl
  third_party/lvgl/src
)

add_compile_definitions(
  STM32F429xx
  USE_HAL_DRIVER
)

# ------------------------------------------------------------------
# Logging switch (app/log.h)
# ------------------------------------------------------------------
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

# Only the HAL modules this project uses (their HAL_*_MODULE_ENABLED flags are
# set in app/stm32f4xx_hal_conf.h so each file compiles cleanly).
set(HAL_SOURCES
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
  # SAI1 block A: I2S master TX to the WM8978
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sai.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sai_ex.c
  # microSD over SDIO (4-bit, DMA receive on DMA2 Stream6)
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sd.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_sdmmc.c
  # FMC: LCD (Bank1 NE1) and SDRAM (Bank2)
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sram.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sdram.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_fmc.c
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

# ChaN FatFs.  diskio.c is not compiled: app/fs_diskio.c owns the SD transport.
set(FATFS_SOURCES
  third_party/FatFs/ff.c
  third_party/FatFs/ffunicode.c
  third_party/FatFs/ffsystem.c
)

# LVGL v8, recursive over src/.
file(GLOB_RECURSE LVGL_SOURCES third_party/lvgl/src/*.c)

# libjpeg decompressor: the exact file set the ALIENTEK 实验47 project built
# (compressors, BMP/PPM writers and cjpeg/djpeg tools are not needed).
set(JPEG_SOURCES
  third_party/jpeg/jaricom.c
  third_party/jpeg/jcomapi.c
  third_party/jpeg/jutils.c
  third_party/jpeg/jerror.c
  third_party/jpeg/jmemmgr.c
  third_party/jpeg/jmemnobs.c
  third_party/jpeg/jdapimin.c
  third_party/jpeg/jdapistd.c
  third_party/jpeg/jdarith.c
  third_party/jpeg/jdmaster.c
  third_party/jpeg/jdinput.c
  third_party/jpeg/jdmarker.c
  third_party/jpeg/jdhuff.c
  third_party/jpeg/jdmainct.c
  third_party/jpeg/jdcoefct.c
  third_party/jpeg/jdpostct.c
  third_party/jpeg/jddctmgr.c
  third_party/jpeg/jidctflt.c
  third_party/jpeg/jidctfst.c
  third_party/jpeg/jidctint.c
  third_party/jpeg/jdsample.c
  third_party/jpeg/jdcolor.c
  third_party/jpeg/jquant1.c
  third_party/jpeg/jquant2.c
  third_party/jpeg/jdmerge.c
)

# Keep the vendored libraries' warnings out of the build so the project's own
# code stays strictly warning-clean under -Wall -Wextra.
set_source_files_properties(${LVGL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(${JPEG_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

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
  ${JPEG_SOURCES}
)

# The FreeRTOS ARM_CM4F port has unconditional vstmdbeq/vldmiaeq {s16-s31} in
# its naked PendSV handler; with -mfloat-abi=soft the assembler disables the FPU
# instruction set, so compile just this file with softfp.
set_source_files_properties(
  third_party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c
  PROPERTIES COMPILE_OPTIONS "-mfloat-abi=softfp;-mfpu=fpv4-sp-d16"
)

# libjpeg's IDCT works on integers but three of its filters are written with
# doubles; -O2 keeps the decode time acceptable on a 168 MHz M4.
set_source_files_properties(
  ${JPEG_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-w;-O2"
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
