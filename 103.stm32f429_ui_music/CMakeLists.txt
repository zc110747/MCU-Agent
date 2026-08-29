cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/arm-none-eabi-gcc.cmake CACHE FILEPATH "" FORCE)
project(stm32f429_ui_music C ASM)

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

# Zero-warning is a hard requirement.  Optimization level depends on build type:
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
  app/ui
  bsp
  sys_startup
  Drivers/CMSIS/Include
  Drivers/STM32F4xx_HAL_Driver/Inc
  third_party/FreeRTOS-Kernel/include
  third_party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F
  third_party/FatFs
  third_party/audio/minimp3
  third_party/tinyusb/src
  # LVGL: both the top dir (for lvgl.h -> "lv_conf.h") and src/ (for the
  # internal "#include ../lvgl.h" relative form used inside lvgl sources).
  third_party/lvgl
  third_party/lvgl/src
)

# TinyUSB configuration: host stack on STM32F4, FreeRTOS OSAL.
add_compile_definitions(
  STM32F429xx
  USE_HAL_DRIVER
  CFG_TUSB_MCU=OPT_MCU_STM32F4
  CFG_TUSB_OS=OPT_OS_FREERTOS
)

# ------------------------------------------------------------------
# Global logging switch (app/log.h)
# ------------------------------------------------------------------
#   PRINT_LOG_ENABLE=1 -> PRINT_LOG() routes to printf_log() -> uart_write()
#   PRINT_LOG_ENABLE=0 -> every PRINT_LOG() compiles to nothing, so the log
#                         costs no FLASH, no stack and no UART bandwidth.
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
file(GLOB UI_SOURCES app/ui/*.c)
file(GLOB BSP_SOURCES
  bsp/bsp_delay.c
  bsp/bsp_led.c
  bsp/bsp_uart.c
  bsp/bsp_i2c.c
  bsp/bsp_pcf8574.c
  # I2C2 sensors: AP3216C (light/proximity/IR) and MPU9250 (9-axis IMU)
  bsp/bsp_ap3216.c
  bsp/bsp_mpu9250.c
  # Capacitive touch: software I2C + GT9147 + T_PEN EXTI + LVGL indev
  bsp/bsp_sw_i2c.c
  bsp/bsp_gt9147.c
  bsp/bsp_touch.c
  bsp/bsp_sdram.c
  bsp/bsp_sdio.c
  bsp/bsp_usb_hw.c
  # WM8978 CODEC (I2C2 control) + SAI1 master-TX audio link (DMA double buffer)
  bsp/bsp_wm8978.c
  bsp/bsp_sai_audio.c
  # LCD (FMC 8080 16-bit) + GBK font stack + LVGL display port
  bsp/bsp_lcd.c
  bsp/bsp_lcd_text.c
  bsp/lcd_ascii_font.c
  bsp/lv_font_gbk.c
  bsp/lv_gbk_map.c
  bsp/lv_port_disp.c
  bsp/lv_port_indev.c
  bsp/bsp_rtc.c
  # AT24C02 EEPROM (alarm persistence) on the shared I2C2 bus
  bsp/bsp_eeprom_24c02.c
)

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
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sai.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sai_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sdram.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sram.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_fmc.c
  # microSD over SDIO (4-bit, polled): needs the HAL SD driver plus the
  # low-level SDMMC command/data engine
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sd.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_sdmmc.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rtc.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rtc_ex.c
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

# TinyUSB host stack (MSC class).  CFG_TUSB_MCU/OS supplied via add_compile_definitions.
set(TINYUSB_SOURCES
  third_party/tinyusb/src/tusb.c
  third_party/tinyusb/src/host/usbh.c
  third_party/tinyusb/src/host/hub.c
  third_party/tinyusb/src/class/msc/msc_host.c
  third_party/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c
  third_party/tinyusb/src/portable/synopsys/dwc2/hcd_dwc2.c
  third_party/tinyusb/src/common/tusb_fifo.c
)

# ChaN FatFs R0.15 with exFAT (FF_FS_EXFAT=1 in third_party/FatFs/ffconf.h).
# diskio.c is intentionally NOT compiled: the USB MSC diskio glue lives in
# app/usb_host_app.c (disk_read/write/ioctl via tuh_msc_*).
set(FATFS_SOURCES
  third_party/FatFs/ff.c
  third_party/FatFs/ffunicode.c
  third_party/FatFs/ffsystem.c
)

# LVGL v8 (recursive: every .c under src/).  Both include dirs above are
# needed so "lv_conf.h" and the internal "../lvgl.h" resolve.
file(GLOB_RECURSE LVGL_SOURCES third_party/lvgl/src/*.c)

# Third-party library: keep its warnings out of the build so the project's own
# code stays strictly warning-clean under -Wall -Wextra.  LVGL is mature and
# vendored, so silencing it here does not hide issues in our source.
set_source_files_properties(${LVGL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

# ------------------------------------------------------------------
# Executable
# ------------------------------------------------------------------
set(CMAKE_EXECUTABLE_SUFFIX .elf)

add_executable(${PROJECT_NAME}.elf
  ${APP_SOURCES}
  ${UI_SOURCES}
  ${BSP_SOURCES}
  ${CMSIS_SOURCES}
  ${HAL_SOURCES}
  ${FREERTOS_SOURCES}
  ${TINYUSB_SOURCES}
  ${FATFS_SOURCES}
  ${LVGL_SOURCES}
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
