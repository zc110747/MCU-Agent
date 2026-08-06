cmake_minimum_required(VERSION 3.20)

project(h743_tinyusb_cdc
        VERSION 1.0.0
        LANGUAGES C ASM)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
# Which USB controller the board's USB socket is wired to:
#   FS -> USB2_OTG_FS on PA11/PA12  (TinyUSB rhport 0)   <- default
#   HS -> USB1_OTG_HS on PB14/PB15, internal FS PHY (TinyUSB rhport 1)
set(USB_PORT "FS" CACHE STRING "USB controller: FS (PA11/PA12) or HS (PB14/PB15)")
set_property(CACHE USB_PORT PROPERTY STRINGS FS HS)

if(USB_PORT STREQUAL "FS")
  set(BOARD_TUD_RHPORT 0)
elseif(USB_PORT STREQUAL "HS")
  set(BOARD_TUD_RHPORT 1)
else()
  message(FATAL_ERROR "USB_PORT must be FS or HS (got '${USB_PORT}')")
endif()
message(STATUS "USB port: OTG_${USB_PORT} -> TinyUSB rhport ${BOARD_TUD_RHPORT}")

set(LIB_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(TINYUSB_DIR  ${LIB_DIR}/tinyusb)
set(CMSIS_CORE   ${LIB_DIR}/cmsis_core/CMSIS/Core/Include)
set(CMSIS_DEVICE ${LIB_DIR}/cmsis_device_h7)
set(HAL_DIR      ${LIB_DIR}/stm32h7xx_hal_driver)

foreach(_d ${TINYUSB_DIR} ${CMSIS_CORE} ${CMSIS_DEVICE} ${HAL_DIR})
  if(NOT EXISTS ${_d})
    message(FATAL_ERROR "Missing dependency: ${_d}\nRun the clone commands in README.md first.")
  endif()
endforeach()

set(TARGET ${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(APP_SOURCES
  src/main.c
  src/bsp.c
  src/usb_descriptors.c
  src/stm32h7xx_it.c
  src/syscalls.c
  src/sdcard.c
  src/fatfs_diskio.c
  src/msc_disk.c
  src/sd_app.c
  ${CMSIS_DEVICE}/Source/Templates/system_stm32h7xx.c
  ${CMSIS_DEVICE}/Source/Templates/gcc/startup_stm32h743xx.s
)

# Only the HAL modules we actually need (unused code is GC'd anyway).
set(HAL_SOURCES
  ${HAL_DIR}/Src/stm32h7xx_hal.c
  ${HAL_DIR}/Src/stm32h7xx_hal_cortex.c
  ${HAL_DIR}/Src/stm32h7xx_hal_rcc.c
  ${HAL_DIR}/Src/stm32h7xx_hal_rcc_ex.c
  ${HAL_DIR}/Src/stm32h7xx_hal_gpio.c
  ${HAL_DIR}/Src/stm32h7xx_hal_pwr.c
  ${HAL_DIR}/Src/stm32h7xx_hal_pwr_ex.c
  ${HAL_DIR}/Src/stm32h7xx_hal_sd.c
  ${HAL_DIR}/Src/stm32h7xx_ll_sdmmc.c
)

set(TINYUSB_SOURCES
  ${TINYUSB_DIR}/src/tusb.c
  ${TINYUSB_DIR}/src/common/tusb_fifo.c
  ${TINYUSB_DIR}/src/device/usbd.c
  ${TINYUSB_DIR}/src/class/cdc/cdc_device.c
  ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dcd_dwc2.c
  ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dwc2_common.c
  ${TINYUSB_DIR}/src/class/msc/msc_device.c
)

set(FATFS_SOURCES
  ${LIB_DIR}/fatfs/source/ff.c
  ${LIB_DIR}/fatfs/source/ffsystem.c
)

add_executable(${TARGET} ${APP_SOURCES} ${HAL_SOURCES} ${TINYUSB_SOURCES} ${FATFS_SOURCES})

target_include_directories(${TARGET} PRIVATE
  src
  ${CMSIS_CORE}
  ${CMSIS_DEVICE}/Include
  ${HAL_DIR}/Inc
  ${TINYUSB_DIR}/src
  ${LIB_DIR}/fatfs/source
)

target_compile_definitions(${TARGET} PRIVATE
  STM32H743xx
  USE_HAL_DRIVER
  CFG_TUSB_MCU=OPT_MCU_STM32H7
  CFG_TUSB_OS=OPT_OS_NONE
  BOARD_TUD_RHPORT=${BOARD_TUD_RHPORT}
  USB_PORT_IS_${USB_PORT}=1
)

target_compile_options(${TARGET} PRIVATE
  -Wall -Wextra
  -Wno-unused-parameter
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
set_source_files_properties(${TINYUSB_SOURCES} ${HAL_SOURCES} ${FATFS_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare")

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/ldscript/stm32h743zi_flash.ld)

target_link_options(${TARGET} PRIVATE
  -T${LINKER_SCRIPT}
  --specs=nano.specs
  --specs=nosys.specs
  -Wl,--gc-sections
  -Wl,--no-warn-rwx-segments
  -Wl,--print-memory-usage
  -Wl,-Map=${TARGET}.map,--cref
  -u _printf_float
)
set_target_properties(${TARGET} PROPERTIES
  SUFFIX ".elf"
  LINK_DEPENDS ${LINKER_SCRIPT})

# ---------------------------------------------------------------------------
# Post-build: hex/bin + size report
# ---------------------------------------------------------------------------
add_custom_command(TARGET ${TARGET} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET}> ${TARGET}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> ${TARGET}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
  COMMENT "Generating .hex / .bin and size report")

# ---------------------------------------------------------------------------
# Convenience targets: flash / erase / reset via OpenOCD
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
endif()
