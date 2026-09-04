# =============================================================================
#  STM32H743ZIT6 OLED Chinese-text demo  -  CMake build
#
#  All paths in this file are RELATIVE to the source tree or use CMake
#  variables (CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR), so the generated build
#  is portable and contains no machine-specific absolute paths.
# =============================================================================
cmake_minimum_required(VERSION 3.22)

# Must be set before project() so the toolchain is loaded for every language.
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake)

project(lvgl_oled C ASM)

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

# ---- Font engine ----------------------------------------------------------
#  2 = CTF index + the original .ttf  (default)
#        A .ctf built by tools/ttf2ctf maps a code point to its glyf offset in
#        three cached reads, so measuring a glyph never touches the TTF and a
#        character the font lacks is rejected with no I/O at all.  Latin falls
#        back to the compiled-in Montserrat bitmaps.
#  1 = HarmonyOS Sans TC outlines streamed straight from the card through
#        LVGL's tiny_ttf (the previous engine, kept for comparison)
#  0 = classic GBK bitmap fonts read from 1:/SYSTEM/FONT/GBKxx.FON
#
#  Override from the command line with e.g.  cmake -DLV_FONT_ENGINE=0 ..
#
#  NOTE: the cached value sticks once the build directory exists, so pass
#  -DLV_FONT_ENGINE=<n> explicitly when switching rather than editing this line.
set(LV_FONT_ENGINE 2 CACHE STRING "2 = CTF+TTF, 1 = HarmonyOS TTF, 0 = GBK bitmap")
message(STATUS "Font engine: ${LV_FONT_ENGINE} (2 = CTF+TTF, 1 = HarmonyOS TTF, 0 = GBK)")

# ---- Compile options --------------------------------------------------------
#  -fdebug-prefix-map maps the absolute source root to "." so the debug info
#    embedded in the .elf references "./Core/Src/main.c" instead of a full
#    path -> portable debug files (no absolute paths).
#  -finput-charset=UTF-8 (and NO -fexec-charset): LVGL parses label text as
#    UTF-8, so the literals must stay UTF-8 in flash.  The UTF-8 code point is
#    translated to the GBK index the GBKxx.FON files use inside the font
#    driver - see Bsp/lv_gbk_map.c.
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
  LV_FONT_ENGINE=${LV_FONT_ENGINE}
)

# ---- Per-config optimisation / debug flags ----------------------------------
set(CMAKE_C_FLAGS_DEBUG   "-Og -g3 -gdwarf-4")
set(CMAKE_C_FLAGS_RELEASE "-O2 -g")
set(CMAKE_ASM_FLAGS        "${MCU_FLAGS_STR} -x assembler-with-cpp")

# ---- Linker -----------------------------------------------------------------
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/sys_startup/STM32H743ZITX_FLASH.ld)
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
include_directories(
  Core/Inc
  Bsp
  Bsp/font
  Application
  Drivers/CMSIS/Include
  sys_startup
  Drivers/STM32H7xx_HAL_Driver/Inc
  third_party/FatFs
  third_party/lvgl
  third_party/lvgl/src
  # stb_truetype lives next to LVGL's tiny_ttf; stb_adapter.c compiles it with
  # its own stream macros and needs the header on the path.
  ${CMAKE_SOURCE_DIR}/third_party/lvgl/src/extra/libs/tiny_ttf
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

# ---- Project sources --------------------------------------------------------
set(PROJECT_SRCS
  # Startup / system
  sys_startup/gcc/startup_stm32h743xx.s
  sys_startup/system_stm32h7xx.c
  # Board / application
  Core/Src/main.c
  Core/Src/stm32h7xx_hal_msp.c
  Core/Src/stm32h7xx_it.c
  Core/Src/syscalls.c
  # BSP: OLED driver + SD-backed Chinese font reader + SDIO glue + RTC
  Bsp/drv_spi_oled.c
  Bsp/drv_oled_fonts.c
  Bsp/drv_oled_text.c
  Bsp/drv_sdio.c
  Bsp/drv_rtc.c
  Bsp/disk_interface.c
  # BSP: crash-safe formatted logger (PRINT_LOG -> uart_write on USART1)
  Bsp/log.c
  # BSP: LVGL glue - display flush, cached FatFs driver, font engines
  Bsp/lv_port_disp.c
  Bsp/lv_port_fs.c
  Bsp/lv_font_gbk.c
  Bsp/lv_font_harmony.c
  Bsp/lv_font_provider.c
  Bsp/lv_gbk_map.c
  # BSP: CTF index + cached TTF reader (HarmonyOS font backend, Phase 3/4/5)
  Bsp/font/blkcache.c
  Bsp/font/ttf_reader.c
  Bsp/font/ctf_reader.c
  # BSP: LRU glyph cache (160 KB, RAM_D2) + TTF glyph preload
  Bsp/font/glyph_cache.c
  # BSP: CTF font engine (Phase 6 stb adapter + Phase 7 LVGL backend)
  Bsp/font/stb_adapter.c
  Bsp/font/lvgl_font.c
  # Demo application
  Application/app_main.c
  Application/app_ui.c
  # UI pages (each page in its own file; shared primitives in ui_common)
  Application/ui_common.c
  Application/ui_page_info.c
  Application/ui_page_font.c
  Application/ui_page_boot.c
  Application/ui_page_fault.c
  # FatFs (dispatch + core + unicode + system + our disk interface)
  third_party/FatFs/diskio.c
  third_party/FatFs/ff.c
  third_party/FatFs/ffsystem.c
  third_party/FatFs/ffunicode.c
)

add_executable(${PROJECT_NAME}.elf
  ${PROJECT_SRCS}
  ${HAL_DRIVER_SRCS}
  ${LVGL_SRCS}
)

# ---- Post build: size, .hex, .bin ------------------------------------------
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
  COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${PROJECT_NAME}.elf>
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}.elf>
          ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf>
          ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
  COMMENT "Generating ${PROJECT_NAME}.hex / .bin and printing memory usage"
)
