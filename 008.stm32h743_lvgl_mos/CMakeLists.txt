# =============================================================================
#  STM32H743ZIT6  -  LVGL menu shell + NES emulator  -  CMake build
#
#  All paths in this file are RELATIVE to the source tree or use CMake
#  variables (CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR), so the generated build
#  is portable and contains no machine-specific absolute paths.
#
#  Layout
#    app/          application logic  (menu, pages, console commands)
#    bsp/          board drivers      (panel, SD, RTC, USB, console, keys)
#    Core/         CubeMX-style startup, clocks, IRQ table
#    Drivers/      ST HAL + CMSIS
#    third_party/  FatFs, LVGL, tinyusb, NES core
# =============================================================================
cmake_minimum_required(VERSION 3.22)

# Must be set before project() so the toolchain is loaded for every language.
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake)

project(nes_h743 C ASM)

# Emit build/compile_commands.json so VS Code IntelliSense (c_cpp_properties.json)
# picks up the exact include paths and defines used by the real build.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Build type -------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (Debug/Release)" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ---- MCU / FPU (a CMake LIST: one element per flag) ------------------------
# Cortex-M7, hard float, fpv5-d16 (double-precision FPU on H7).
set(MCU_FLAGS
  -mcpu=cortex-m7
  -mthumb
  -mfloat-abi=hard
  -mfpu=fpv5-d16
)
# Same flags as a single space-separated string for ASM / linker stages.
list(JOIN MCU_FLAGS " " MCU_FLAGS_STR)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# ---- Compile options --------------------------------------------------------
#  -fdebug-prefix-map maps the absolute source root to "." so the debug info
#    embedded in the .elf references "./Core/Src/main.c" instead of a full
#    path -> portable debug files (no absolute paths).
#  -finput-charset=UTF-8 (and matching -fexec-charset): LVGL parses label text
#    as UTF-8, so the literals must stay UTF-8 in flash.  The UTF-8 code point
#    is translated to the GBK index the GBKxx.FON files use inside the font
#    driver - see bsp/lv_gbk_map.c.
add_compile_options(
  ${MCU_FLAGS}
  -fdata-sections
  -ffunction-sections
  -fstack-usage
  -fdebug-prefix-map=${CMAKE_SOURCE_DIR}=.
  -finput-charset=UTF-8
  -fexec-charset=UTF-8
)

add_compile_definitions(
  STM32H743xx
  USE_HAL_DRIVER
  HSE_VALUE=25000000
  # tinyusb needs the MCU family on the command line as well as in
  # tusb_config.h: some of its headers are pulled in before the config.
  CFG_TUSB_MCU=OPT_MCU_STM32H7
  CFG_TUSB_OS=OPT_OS_NONE
)

# ---- Per-config optimisation / debug flags ----------------------------------
#  The NES core is interpreted 6502 + a scanline PPU: at -Og it does not make
#  60 fps, so Release (-O2) is the configuration to use for actual gaming.
set(CMAKE_C_FLAGS_DEBUG   "-Og -g3 -gdwarf-4")
set(CMAKE_C_FLAGS_RELEASE "-O2 -g")
set(CMAKE_ASM_FLAGS        "${MCU_FLAGS_STR} -x assembler-with-cpp")

# ---- Linker -----------------------------------------------------------------
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/STM32H743ZITX_FLASH.ld)
set(CMAKE_EXE_LINKER_FLAGS
    "${MCU_FLAGS_STR} \
     -T${LINKER_SCRIPT} \
     -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map \
     -Wl,--gc-sections \
     -Wl,--print-memory-usage \
     -Wl,--no-warn-rwx-segments \
     -specs=nano.specs")

# ---- Include directories (all relative to the source root) ------------------
#  third_party/lvgl must be on the path so that the __has_include("lv_conf.h")
#  probe inside lv_conf_internal.h finds our third_party/lvgl/lv_conf.h.
#  bsp/ carries tusb_config.h, which tinyusb includes by plain name.
include_directories(
  Core/Inc
  app
  bsp
  Drivers/CMSIS/Include
  Drivers/CMSIS/Device/ST/STM32H7xx/Include
  Drivers/STM32H7xx_HAL_Driver/Inc
  third_party/FatFs
  third_party/lvgl
  third_party/lvgl/src
  third_party/nes
  third_party/tinyusb/src
  third_party/tjpgd
)

# ---- HAL driver sources (glob, then drop the HAL template files) ------------
#  The *_template.c files redefine HAL_MspInit / HAL_InitTick and would either
#  clash with our stm32h7xx_hal_msp.c or silently (and wrongly) replace the
#  SysTick-based tick handler, so they are excluded from the build.
file(GLOB HAL_DRIVER_SRCS CONFIGURE_DEPENDS
     Drivers/STM32H7xx_HAL_Driver/Src/*.c)
list(FILTER HAL_DRIVER_SRCS EXCLUDE REGEX ".*_template\\.c$")

# ---- LVGL sources -----------------------------------------------------------
#  LVGL is built the way upstream intends: compile everything and let the
#  #if guards driven by lv_conf.h empty out the disabled widgets/drivers.
#  --gc-sections then drops whatever is left unreferenced.
file(GLOB_RECURSE LVGL_SRCS CONFIGURE_DEPENDS
     third_party/lvgl/src/*.c)

# ---- tinyusb ----------------------------------------------------------------
#  Hand-picked instead of globbed: the upstream tree ships every port and every
#  class it supports, and compiling the lot would drag in host-mode files that
#  do not build without CFG_TUH_ENABLED.  Device stack + CDC + the dwc2 port
#  (which is what the H7's OTG core is) is all this board needs.
set(TINYUSB_SRCS
  third_party/tinyusb/src/tusb.c
  third_party/tinyusb/src/common/tusb_fifo.c
  third_party/tinyusb/src/device/usbd.c
  third_party/tinyusb/src/class/cdc/cdc_device.c
  third_party/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c
  third_party/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c
)

# ---- NES core ---------------------------------------------------------------
set(NES_SRCS
  third_party/nes/nes_cpu.c
  third_party/nes/nes_ppu.c
  third_party/nes/nes_bus.c
  third_party/nes/nes_mapper.c
  third_party/nes/nes_main.c
)

# ---- TJpgDec ----------------------------------------------------------------
#  ChaN's Tiny JPEG Decompressor, used by the image viewer.  LVGL ships the
#  same R0.03 sources under extra/libs/sjpg, but they are wrapped in
#  "#if LV_USE_SJPG" and hard-wired to RGB888 output.  Enabling LV_USE_SJPG
#  would also pull in lv_sjpg.c and the lv_fs layer, neither of which this
#  build has any use for, so a local copy is kept with JD_FORMAT=1 (RGB565
#  straight to the ST7789) and JD_FASTDECODE=1.  Vendor code: not held to the
#  strict warning set below.
set(TJPGD_SRCS
  third_party/tjpgd/tjpgd.c
)

# ---- FatFs ------------------------------------------------------------------
#  Vendor code (ChaN): kept out of the strict warning set below.
set(FATFS_SRCS
  third_party/FatFs/diskio.c
  third_party/FatFs/ff.c
  third_party/FatFs/ffsystem.c
  third_party/FatFs/ffunicode.c
)

# ---- Project sources --------------------------------------------------------
set(PROJECT_SRCS
  # Startup / system
  Core/Src/startup_stm32h743xx.s
  Core/Src/system_stm32h7xx.c
  # Board / interrupt plumbing
  Core/Src/main.c
  Core/Src/stm32h7xx_hal_msp.c
  Core/Src/stm32h7xx_it.c
  Core/Src/syscalls.c
  # BSP: panel + SD-backed Chinese font reader + SDIO glue + RTC
  bsp/drv_spi_oled.c
  bsp/drv_oled_fonts.c
  bsp/drv_oled_text.c
  bsp/drv_sdio.c
  bsp/drv_rtc.c
  bsp/disk_interface.c
  # BSP: LVGL glue - display flush, SD-backed GBK font, Unicode->GBK table
  bsp/lv_port_disp.c
  bsp/lv_font_gbk.c
  bsp/lv_gbk_map.c
  # BSP: console (UART + USB CDC) and the virtual key layer
  bsp/bsp_console.c
  bsp/bsp_key.c
  bsp/drv_usb_cdc.c
  bsp/usb_descriptors.c
  # BSP: GBK(8.3 names) -> UTF-8 transcoder for the serial console
  bsp/gbk_conv.c
  bsp/gbk_unicode_tbl.c
  # Application: menu shell, pages, emulator front-end, console parser
  app/app_main.c
  app/app_menu.c
  app/app_pages.c
  app/menu_icons.c
  app/sd_browser.c
  app/page_nes.c
  app/img_decode.c
  app/page_image.c
  app/app_cmd.c
)

add_executable(${PROJECT_NAME}.elf
  ${PROJECT_SRCS}
  ${FATFS_SRCS}
  ${HAL_DRIVER_SRCS}
  ${LVGL_SRCS}
  ${TINYUSB_SRCS}
  ${NES_SRCS}
  ${TJPGD_SRCS}
)

# Our own code is held to -Wall -Wextra; the vendor trees (HAL, LVGL, tinyusb)
# are not, because they are not ours to fix and the noise would bury real
# warnings.  set_source_files_properties keeps the distinction per file.
set_source_files_properties(${PROJECT_SRCS} ${NES_SRCS}
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")

# ---- Post build: size, .hex, .bin ------------------------------------------
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
  COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${PROJECT_NAME}.elf>
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}.elf>
          ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf>
          ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
  COMMENT "Generating ${PROJECT_NAME}.hex / .bin and printing memory usage"
)
