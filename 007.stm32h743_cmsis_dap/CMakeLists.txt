cmake_minimum_required(VERSION 3.20)

project(h743_cmsis_dap
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

# The OTG transceiver is powered from the VDD33USB domain, which can be fed by
# the on-chip LDO (USBREGEN=1) or by an external 3.3 V rail (USBREGEN=0).
# If the board does NOT route an external 3.3 V to VDD33USB, the internal LDO
# MUST be enabled or the host never sees the device ("USB not detected").
# Leave this ON unless your board schematic shows VDD33USB wired to 3.3V.
option(DAP_USB_INTERNAL_REGULATOR "Power VDD33USB from the on-chip USB LDO (USBREGEN)" OFF)

set(APP_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/app)
set(BSP_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/bsp)
set(THIRD_PARTY  ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
set(TINYUSB_DIR  ${THIRD_PARTY}/tinyusb)
set(CMSISDAP_DIR ${THIRD_PARTY}/CMSIS-DAP)
set(DRIVERS_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/Drivers)
set(HAL_DIR      ${DRIVERS_DIR}/STM32H7xx_HAL_Driver)
set(CMSIS_CORE   ${DRIVERS_DIR}/CMSIS/Core/Include)
set(CMSIS_DEVICE ${CMAKE_CURRENT_SOURCE_DIR}/sys_startup)

foreach(_d ${TINYUSB_DIR} ${CMSISDAP_DIR} ${CMSIS_CORE} ${CMSIS_DEVICE} ${HAL_DIR})
  if(NOT EXISTS ${_d})
    message(FATAL_ERROR "Missing dependency: ${_d}\nSee README.md for how to populate Drivers/ and third_party/.")
  endif()
endforeach()

set(TARGET ${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(APP_SOURCES
  ${APP_DIR}/main.c
  ${APP_DIR}/usb_descriptors.c
  ${APP_DIR}/stm32h7xx_it.c
  ${APP_DIR}/syscalls.c
  ${BSP_DIR}/bsp.c
  ${BSP_DIR}/dap_port.c
  sys_startup/system_stm32h7xx.c
  sys_startup/gcc/startup_stm32h743xx.s
)

# ARM's reference CMSIS-DAP firmware. SWO.c is deliberately absent: it pulls in
# CMSIS-Driver USART + CMSIS-RTOS2, and SWO is not part of the v1 baseline.
set(CMSISDAP_SOURCES
  ${CMSISDAP_DIR}/DAP.c
  ${CMSISDAP_DIR}/SW_DP.c
  ${CMSISDAP_DIR}/JTAG_DP.c
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
)

set(TINYUSB_SOURCES
  ${TINYUSB_DIR}/src/tusb.c
  ${TINYUSB_DIR}/src/common/tusb_fifo.c
  ${TINYUSB_DIR}/src/device/usbd.c
  ${TINYUSB_DIR}/src/class/hid/hid_device.c
  ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dcd_dwc2.c
  ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dwc2_common.c
)

add_executable(${TARGET} ${APP_SOURCES} ${CMSISDAP_SOURCES} ${HAL_SOURCES} ${TINYUSB_SOURCES})

target_include_directories(${TARGET} PRIVATE
  ${APP_DIR}
  ${BSP_DIR}
  ${CMSISDAP_DIR}
  ${CMSIS_CORE}
  sys_startup
  ${HAL_DIR}/Inc
  ${TINYUSB_DIR}/src
)

target_compile_definitions(${TARGET} PRIVATE
  STM32H743xx
  USE_HAL_DRIVER
  CFG_TUSB_MCU=OPT_MCU_STM32H7
  CFG_TUSB_OS=OPT_OS_NONE
  BOARD_TUD_RHPORT=${BOARD_TUD_RHPORT}
  USB_PORT_IS_${USB_PORT}=1
  DAP_USB_INTERNAL_REGULATOR=$<BOOL:${DAP_USB_INTERNAL_REGULATOR}>
)

target_compile_options(${TARGET} PRIVATE
  -Wall -Wextra
  -Wno-unused-parameter
  -ffunction-sections
  -fdata-sections
  -fno-common
  $<$<CONFIG:Debug>:-Og>
  $<$<CONFIG:Debug>:-g3>
  $<$<CONFIG:Debug>:-gdwarf-4>
  $<$<CONFIG:Release>:-O2>
  $<$<CONFIG:Release>:-g>
)

# The DAP bit-banging loops are the one place where optimisation level is a
# functional property, not a preference: at -Og the SWD/JTAG inner loops are
# slow and lumpy enough to distort the generated clock. Always build them -O2,
# even in a Debug build, and keep full debug info so they stay steppable.
set_source_files_properties(${CMSISDAP_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2")

# TinyUSB and the ST HAL are third-party: don't fail our build on their warnings
set_source_files_properties(${TINYUSB_SOURCES} ${HAL_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare")

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

# ---------------------------------------------------------------------------
# Post-build: hex/bin + size report
# ---------------------------------------------------------------------------
add_custom_command(TARGET ${TARGET} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET}> ${TARGET}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> ${TARGET}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
  COMMENT "Generating .hex / .bin and size report")

# ---------------------------------------------------------------------------
# Convenience targets driven through OpenOCD
#
#   flash / erase / reset : ST-Link -> this board (programming the probe)
#   dap-test              : this board -> STM32F429 (using the probe)
# ---------------------------------------------------------------------------
find_program(OPENOCD_EXE openocd)
if(OPENOCD_EXE)
  set(OOCD_CFG      ${CMAKE_CURRENT_SOURCE_DIR}/openocd/stm32h743_stlink.cfg)
  set(OOCD_DAP_F429 ${CMAKE_CURRENT_SOURCE_DIR}/openocd/stm32f429_cmsisdap.cfg)

  add_custom_target(flash
    COMMAND ${OPENOCD_EXE} -f ${OOCD_CFG}
            -c "program $<TARGET_FILE:${TARGET}> verify reset exit"
    DEPENDS ${TARGET}
    COMMENT "Flashing the probe firmware via ST-Link")

  add_custom_target(erase
    COMMAND ${OPENOCD_EXE} -f ${OOCD_CFG}
            -c "init" -c "reset halt" -c "stm32h7x mass_erase 0" -c "exit"
    COMMENT "Mass erasing flash bank 0")

  add_custom_target(reset
    COMMAND ${OPENOCD_EXE} -f ${OOCD_CFG} -c "init" -c "reset run" -c "exit"
    COMMENT "Resetting the probe")

  add_custom_target(dap-test
    COMMAND ${OPENOCD_EXE} -f ${OOCD_DAP_F429}
            -c "init" -c "dap info" -c "targets" -c "exit"
    COMMENT "Probing an STM32F429 through our own CMSIS-DAP interface")

  # Non-destructive self-test: confirm the DP IDCODE and exercise the memory
  # read/write path (download) by writing a sentinel to SRAM and reading it
  # back. No flash is touched.
  add_custom_target(dap-verify
    COMMAND ${OPENOCD_EXE} -f ${OOCD_DAP_F429}
            -c "init" -c "dap info" -c "halt"
            -c "mww 0x20000000 0xDEADBEEF" -c "mdw 0x20000000"
            -c "resume" -c "targets" -c "exit"
    COMMENT "Self-test through our CMSIS-DAP: IDCODE + SRAM R/W (no flash)")
endif()
