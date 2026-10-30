cmake_minimum_required(VERSION 3.20)

project(h743_usb_mouse
        VERSION 1.0.0
        LANGUAGES C ASM)

# ---------------------------------------------------------------------------
# Dependencies (all local, no network, no CubeMX / CubeIDE / Keil / IAR)
#
# Directory layout:
#   Drivers/          ST upstream tree, byte-for-byte unmodified
#   third_party/      upstream libraries (tinyusb, ...), unmodified
#   sys_startup/      local startup / device headers / linker script
#   bsp/              board support, including bsp/camera (vendor sensor)
#
# The project keeps no top-level drivers/ directory: ST already owns that
# name, and a Windows filesystem would merge the two case-insensitively.
# Own-written sensor glue therefore lives under bsp/.
# ---------------------------------------------------------------------------
set(THIRD_PARTY   ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(TINYUSB_DIR   ${THIRD_PARTY}/tinyusb)
set(ST_DRIVERS    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers)
set(HAL_DIR       ${ST_DRIVERS}/STM32H7xx_HAL_Driver)
set(CMSIS_CORE    ${ST_DRIVERS}/CMSIS/Core/Include)
set(CMSIS_DEVICE  sys_startup)          # local startup/device headers/linker

foreach(_d ${TINYUSB_DIR} ${CMSIS_CORE} ${CMSIS_DEVICE} ${HAL_DIR})
  if(NOT EXISTS ${_d})
    message(FATAL_ERROR "Missing dependency: ${_d}")
  endif()
endforeach()

set(TARGET ${PROJECT_NAME})

# USB is fixed to the FS core on PA11/PA12 (rhport 0).
set(BOARD_TUD_RHPORT 0)

# ---------------------------------------------------------------------------
# Sources - every directory below owns exactly one layer
# ---------------------------------------------------------------------------
set(APP_SOURCES
  app/main.c
)

set(BSP_SOURCES
  bsp/bsp.c
  bsp/bsp_button.c
  bsp/bsp_uart.c
  bsp/bsp_camera.c
  bsp/bsp_ov5640_ref.c
  bsp/stm32h7xx_hal_msp.c
  bsp/stm32h7xx_it.c
  bsp/syscalls.c
  ${CMSIS_DEVICE}/system_stm32h7xx.c
  ${CMSIS_DEVICE}/gcc/startup_stm32h743xx.s
)

set(MOUSE_SOURCES
  mouse/mouse_input.c
  mouse/mouse_core.c
  mouse/mouse_report.c
)

set(USB_SOURCES
  usb/usb_descriptors.c
  usb/usb_hid_mouse.c
)

set(PROTOCOL_SOURCES
  protocol/uart_protocol.c
  protocol/command_handler.c
)

set(ALGORITHM_SOURCES
  algorithms/image_motion.c
  algorithms/motion_filter.c
)

set(DRIVER_SOURCES
  bsp/camera/ov5640.c
  bsp/camera/ov5640_reg.c
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
  ${HAL_DIR}/Src/stm32h7xx_hal_i2c.c
  ${HAL_DIR}/Src/stm32h7xx_hal_i2c_ex.c
  ${HAL_DIR}/Src/stm32h7xx_hal_dcmi.c
)

set(TINYUSB_SOURCES
  ${TINYUSB_DIR}/src/tusb.c
  ${TINYUSB_DIR}/src/common/tusb_fifo.c
  ${TINYUSB_DIR}/src/device/usbd.c
  ${TINYUSB_DIR}/src/class/hid/hid_device.c
  ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dcd_dwc2.c
  ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dwc2_common.c
)

add_executable(${TARGET}
  ${APP_SOURCES} ${BSP_SOURCES} ${MOUSE_SOURCES} ${USB_SOURCES}
  ${PROTOCOL_SOURCES} ${ALGORITHM_SOURCES} ${DRIVER_SOURCES}
  ${HAL_SOURCES} ${TINYUSB_SOURCES})

target_include_directories(${TARGET} PRIVATE
  app
  bsp
  bsp/camera
  mouse
  usb
  protocol
  algorithms
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
  -Wundef
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

# ST HAL / TinyUSB / the vendor camera driver are third-party: their warnings
# are not ours to fix, and -Werror-style noise here would hide real ones.
set_source_files_properties(${TINYUSB_SOURCES} ${HAL_SOURCES} ${DRIVER_SOURCES}
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

# Sanity check: make sure every DMA buffer really landed in AXI SRAM/D2 SRAM
# and that the section is 32-byte aligned (cache-line correctness).
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
