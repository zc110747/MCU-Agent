cmake_minimum_required(VERSION 3.20)

# Cross-compile settings MUST come before project()
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ARM)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
# Bare-metal: skip linking an executable during compiler checks (no syscalls)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

project(stm32_qspi C ASM)

# MCU flags as a CMake LIST (each element becomes a separate compiler argument)
set(MCU_FLAGS
  -mcpu=cortex-m7
  -mfpu=fpv5-d16
  -mfloat-abi=hard
  -mthumb
  -DSTM32H743xx
  -DUSE_HAL_DRIVER
  -DCORE_CM7
)

# Build type default
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Common compile options (lists, not space-joined strings)
add_compile_options(
  ${MCU_FLAGS}
  -Wall
  -Wextra
  -fdata-sections
  -ffunction-sections
  -MMD
  -MP
  $<$<CONFIG:Debug>:-g3>
  $<$<CONFIG:Debug>:-O0>
  $<$<CONFIG:Release>:-Os>
)

# Linker flags
set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/stm32h743xix_flash.ld")
add_link_options(
  -T${LINKER_SCRIPT}
  ${MCU_FLAGS}
  -Wl,--gc-sections
  -Wl,-Map=${CMAKE_PROJECT_NAME}.map
  # No "nosys" specs: bsp/syscalls.c provides the minimal newlib stubs
  # (keeps the link warning-free). Cortex-M has no MMU/XN, so an RWX LOAD
  # segment is harmless; silence the cosmetic GNU ld warning.
  -Wl,--no-warn-rwx-segments
)

# TinyUSB / board selection
set(BOARD_TUD_RHPORT 0)   # OTG_FS on PA11/PA12 (internal FS PHY)

# CMSIS + Startup
file(GLOB_RECURSE CMSIS_SOURCES
  "Drivers/CMSIS/Device/*.c"
  "Drivers/CMSIS/Device/*.s"
)

# HAL sources (all available; unused ones are dropped by --gc-sections)
file(GLOB HAL_SOURCES "Drivers/HAL/Src/*.c")
# Exclude CubeMX template files (they redefine HAL_InitTick etc.)
list(FILTER HAL_SOURCES EXCLUDE REGEX "[Tt]emplate")

# App + BSP sources
file(GLOB APP_SOURCES "app/*.c" "bsp/*.c")
# The boot path uses only HAL indirect mode; the XIP self-test (qspi_test.c) is
# intentionally NOT compiled - keeping it out of the image avoids the memory-
# mapped session that previously wedged the QUADSPI peripheral for later access.
list(FILTER APP_SOURCES EXCLUDE REGEX "app/qspi_test\\.c$")

# FatFs sources (diskio + ff + ffsystem + ffunicode)
set(FATFS_SOURCES
  ${CMAKE_SOURCE_DIR}/third_party/FatFs/diskio.c
  ${CMAKE_SOURCE_DIR}/third_party/FatFs/ff.c
  ${CMAKE_SOURCE_DIR}/third_party/FatFs/ffsystem.c
  ${CMAKE_SOURCE_DIR}/third_party/FatFs/ffunicode.c
)

# TinyUSB sources (DWC2 port only - what STM32H7 uses)
set(TINYUSB_DIR "${CMAKE_SOURCE_DIR}/third_party/tinyusb")
set(TINYUSB_SOURCES
  ${TINYUSB_DIR}/src/tusb.c
  ${TINYUSB_DIR}/src/common/tusb_fifo.c
  ${TINYUSB_DIR}/src/device/usbd.c
  ${TINYUSB_DIR}/src/class/msc/msc_device.c
  ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dcd_dwc2.c
  ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dwc2_common.c
)

# Executable
add_executable(${CMAKE_PROJECT_NAME}.elf
  ${CMSIS_SOURCES}
  ${HAL_SOURCES}
  ${APP_SOURCES}
  ${TINYUSB_SOURCES}
  ${FATFS_SOURCES}
)

target_include_directories(${CMAKE_PROJECT_NAME}.elf PUBLIC
  app
  bsp
  Drivers/CMSIS/Include
  Drivers/CMSIS/Device/ST/STM32H7xx/Include
  Drivers/HAL/Inc
  ${TINYUSB_DIR}/src
  ${CMAKE_SOURCE_DIR}/third_party/FatFs
)

# TinyUSB needs CFG_TUSB_MCU / CFG_TUSB_OS and the rhport selector.
target_compile_definitions(${CMAKE_PROJECT_NAME}.elf PRIVATE
  CFG_TUSB_MCU=OPT_MCU_STM32H7
  CFG_TUSB_OS=OPT_OS_NONE
  BOARD_TUD_RHPORT=${BOARD_TUD_RHPORT}
)

# Third-party libs are not ours; silence their warnings rather than churn them.
set_source_files_properties(${TINYUSB_SOURCES} ${HAL_SOURCES} ${FATFS_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare")

# Convert to binary & hex
add_custom_command(TARGET ${CMAKE_PROJECT_NAME}.elf POST_BUILD
  COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf> ${CMAKE_PROJECT_NAME}.hex
  COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf> ${CMAKE_PROJECT_NAME}.bin
  COMMAND arm-none-eabi-size $<TARGET_FILE:${CMAKE_PROJECT_NAME}.elf>
  COMMENT "Building hex and bin..."
)
