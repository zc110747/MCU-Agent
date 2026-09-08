cmake_minimum_required(VERSION 3.20)

project(h743_usb_serial
        VERSION 1.0.0
        LANGUAGES C ASM)

# ---------------------------------------------------------------------------
# Dependencies (all local, no network, no CubeMX / CubeIDE / Keil / IAR)
# ---------------------------------------------------------------------------
set(THIRD_PARTY   ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(TINYUSB_DIR   ${THIRD_PARTY}/tinyusb)
set(DRIVERS_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/Drivers)
set(HAL_DIR       ${DRIVERS_DIR}/STM32H7xx_HAL_Driver)
set(CMSIS_CORE    ${DRIVERS_DIR}/CMSIS/Core/Include)
set(CMSIS_DEVICE  sys_startup)          # local startup/device headers/linker

foreach(_d ${TINYUSB_DIR} ${CMSIS_CORE} ${CMSIS_DEVICE} ${HAL_DIR})
  if(NOT EXISTS ${_d})
    message(FATAL_ERROR "Missing dependency: ${_d}")
  endif()
endforeach()

set(TARGET ${PROJECT_NAME})

# USB is fixed to the FS core on PA11/PA12 (rhport 0).
# PB14 - the OTG_HS DM pin - is used here as UART4_RTS, so rhport 1 is not
# available on this board.
set(BOARD_TUD_RHPORT 0)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(APP_SOURCES
  app/main.c
  app/ringbuf.c
  app/uart_bridge.c
  app/usb_bridge.c
  app/usb_descriptors.c
)

set(BSP_SOURCES
  bsp/bsp.c
  bsp/stm32h7xx_it.c
  bsp/syscalls.c
  ${CMSIS_DEVICE}/system_stm32h7xx.c
  ${CMSIS_DEVICE}/gcc/startup_stm32h743xx.s
)

# Only the HAL modules this project actually uses.
set(HAL_SOURCES
  ${HAL_DIR}/Src/stm32h7xx_hal.c
  ${HAL_DIR}/Src/stm32h7xx_hal_cortex.c
  ${HAL_DIR}/Src/stm32h7xx_hal_rcc.c
  ${HAL_DIR}/Src/stm32h7xx_hal_rcc_ex.c
  ${HAL_DIR}/Src/stm32h7xx_hal_gpio.c
  ${HAL_DIR}/Src/stm32h7xx_hal_pwr.c
  ${HAL_DIR}/Src/stm32h7xx_hal_pwr_ex.c
  ${HAL_DIR}/Src/stm32h7xx_hal_dma.c
  ${HAL_DIR}/Src/stm32h7xx_hal_dma_ex.c
  ${HAL_DIR}/Src/stm32h7xx_hal_uart.c
  ${HAL_DIR}/Src/stm32h7xx_hal_uart_ex.c
  ${HAL_DIR}/Src/stm32h7xx_hal_flash.c
  ${HAL_DIR}/Src/stm32h7xx_hal_flash_ex.c
  ${HAL_DIR}/Src/stm32h7xx_hal_exti.c
)

set(TINYUSB_SOURCES
  ${TINYUSB_DIR}/src/tusb.c
  ${TINYUSB_DIR}/src/common/tusb_fifo.c
  ${TINYUSB_DIR}/src/device/usbd.c
  ${TINYUSB_DIR}/src/class/cdc/cdc_device.c
  ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dcd_dwc2.c
  ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dwc2_common.c
)

add_executable(${TARGET} ${APP_SOURCES} ${BSP_SOURCES} ${HAL_SOURCES} ${TINYUSB_SOURCES})

target_include_directories(${TARGET} PRIVATE
  app
  bsp
  ${CMSIS_CORE}
  ${CMSIS_DEVICE}
  ${HAL_DIR}/Inc
  ${TINYUSB_DIR}/src
)

target_compile_definitions(${TARGET} PRIVATE
  STM32H743xx
  USE_HAL_DRIVER
  CFG_TUSB_MCU=OPT_MCU_STM32H7
  CFG_TUSB_OS=OPT_OS_NONE
  BOARD_TUD_RHPORT=${BOARD_TUD_RHPORT}
)

target_compile_options(${TARGET} PRIVATE
  -Wall -Wextra
  -Wno-unused-parameter
  -Wdouble-promotion
  -Wshadow
  -ffunction-sections
  -fdata-sections
  -fno-common
  -fstack-usage
  $<$<CONFIG:Debug>:-Og>
  $<$<CONFIG:Debug>:-g3>
  $<$<CONFIG:Debug>:-gdwarf-4>
  $<$<CONFIG:Release>:-Os>
  $<$<CONFIG:Release>:-g>
)

# TinyUSB and the ST HAL are third-party: don't fail our build on their warnings
set_source_files_properties(${TINYUSB_SOURCES} ${HAL_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-w")

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/sys_startup/stm32h743zi_flash.ld)

target_link_options(${TARGET} PRIVATE
  -T${LINKER_SCRIPT}
  --specs=nano.specs
  --specs=nosys.specs
  -Wl,--gc-sections
  -Wl,--no-warn-rwx-segments
  -Wl,--print-memory-usage
  -Wl,-Map=${TARGET}.map,--cref
)
set_target_properties(${TARGET} PROPERTIES
  SUFFIX ".elf"
  LINK_DEPENDS ${LINKER_SCRIPT})

# Sanity check: make sure every DMA buffer really landed in AXI SRAM and that
# the section is 32-byte aligned (cache-line correctness).
add_custom_command(TARGET ${TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -DTARGET_FILE=$<TARGET_FILE:${TARGET}>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_dma_section.cmake
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET}> ${TARGET}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> ${TARGET}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
  COMMENT "Checking .dma_buf placement, generating .hex/.bin and size report")

# ---------------------------------------------------------------------------
# Convenience targets: flash / erase / reset via OpenOCD + ST-Link
# ---------------------------------------------------------------------------
find_program(OPENOCD_EXE openocd)
if(OPENOCD_EXE)
  set(OOCD_CFG ${CMAKE_CURRENT_SOURCE_DIR}/openocd/stm32h743_stlink.cfg)
  add_custom_target(flash
    COMMAND ${OPENOCD_EXE} -f ${OOCD_CFG}
            -c "program $<TARGET_FILE:${TARGET}> verify reset exit"
    DEPENDS ${TARGET}
    COMMENT "Flashing via ST-Link")
  add_custom_target(erase
    COMMAND ${OPENOCD_EXE} -f ${OOCD_CFG}
            -c "init" -c "reset halt" -c "stm32h7x mass_erase 0" -c "exit"
    COMMENT "Mass erasing flash bank 0")
  add_custom_target(reset
    COMMAND ${OPENOCD_EXE} -f ${OOCD_CFG} -c "init" -c "reset run" -c "exit"
    COMMENT "Resetting target")
else()
  message(STATUS "openocd not found in PATH - flash/erase/reset targets disabled")
endif()
