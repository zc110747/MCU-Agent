# =============================================================================
#  STM32H743ZIT6 OLED Chinese-text demo (emWin edition)  -  CMake build
#
#  All paths in this file are RELATIVE to the source tree or use CMake
#  variables (CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR), so the generated build
#  is portable and contains no machine-specific absolute paths.
# =============================================================================
cmake_minimum_required(VERSION 3.22)

# Must be set before project() so the toolchain is loaded for every language.
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake)

project(emwin_oled C ASM)

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
#  -finput-charset=UTF-8 / -fexec-charset=UTF-8: the UI strings are authored
#    as UTF-8 literals; they must stay UTF-8 in flash so emWin's UTF-8 decoder
#    (GUI_UC_SetEncodeUTF8) can turn them into GBK codepoints for the SD fonts.
add_compile_options(
  ${MCU_FLAGS}
  -fdata-sections
  -ffunction-sections
  -fstack-usage
  -fdebug-prefix-map=${CMAKE_SOURCE_DIR}=.
  -finput-charset=UTF-8
  -fexec-charset=UTF-8
  # The STemWin prebuilt library (STemWin_CM7_wc16.a) was built with a 16-bit
  # wchar_t ("wc16"). Our toolchain defaults to 32-bit wchar_t, which makes the
  # EABI objects incompatible and makes GNU ld reject every cross-library call
  # ("Unknown destination type / dangerous relocation"). -fshort-wchar aligns
  # our wchar_t to 16 bits so the objects link cleanly.
  -fshort-wchar
)

add_compile_definitions(
  STM32H743xx
  USE_HAL_DRIVER
  HSE_VALUE=25000000
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
     -Wl,--no-wchar-size-warning \
     -specs=nano.specs")

# ---- Include directories (all relative to the source root) ------------------
include_directories(
  Core/Inc
  Bsp
  Application
  sys_startup
  Drivers/CMSIS/Include
  Drivers/STM32H7xx_HAL_Driver/Inc
  third_party/FatFs
  third_party/STemWin/inc
  third_party/STemWin/Config
  Bsp/emWin
)

# ---- HAL driver sources (glob, then drop the HAL template files) ------------
#  The *_template.c files redefine HAL_MspInit / HAL_InitTick and would either
#  clash with our stm32h7xx_hal_msp.c or silently (and wrongly) replace the
#  SysTick-based tick handler, so they are excluded from the build.
file(GLOB HAL_DRIVER_SRCS CONFIGURE_DEPENDS
     Drivers/STM32H7xx_HAL_Driver/Src/*.c)
list(FILTER HAL_DRIVER_SRCS EXCLUDE REGEX ".*_template\\.c$")

# ---- STemWin prebuilt library (v5.44, CM7, wide-char 16-bit, No-OS) --------
set(STEMWIN_LIB ${CMAKE_SOURCE_DIR}/third_party/STemWin/Lib/STemWin_CM7_wc16.a)

# ---- Project sources --------------------------------------------------------
set(PROJECT_SRCS
  # Startup / system (local device layer in sys_startup, not Drivers/CMSIS/Device)
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
  # BSP: emWin glue - LCD/Lin driver, No-OS system layer, custom GBK font
  Bsp/emWin/GUIConf.c
  Bsp/emWin/GUI_X.c
  Bsp/emWin/LCDConf.c
  Bsp/emWin/emwin_font_gbk.c
  # Unicode -> GBK bridge (compiled table)
  Bsp/lv_gbk_map.c
  # Demo application (emWin port of the 003 LVGL panel)
  Application/app_main.c
  Application/app_ui.c
  # FatFs (dispatch + core + unicode + system + our disk interface)
  third_party/FatFs/diskio.c
  third_party/FatFs/ff.c
  third_party/FatFs/ffsystem.c
  third_party/FatFs/ffunicode.c
)

add_executable(${PROJECT_NAME}.elf
  ${PROJECT_SRCS}
  ${HAL_DRIVER_SRCS}
)

# Make CMake re-link when the linker script changes (otherwise ninja says
# "no work to do" after editing sys_startup/STM32H743ZITX_FLASH.ld).
set_target_properties(${PROJECT_NAME}.elf PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

# ---- STemWin prebuilt library (v5.44, CM7, wc16, No-OS) --------------------
# Linked through target_link_libraries (NOT as an add_executable source, which
# CMake silently drops for a .a). Wrapped in --start-group/--end-group because
# STemWin's own object members have circular references that a single-pass
# link would leave unresolved (undefined reference to GUI_*).
add_library(stemwin STATIC IMPORTED)
set_target_properties(stemwin PROPERTIES IMPORTED_LOCATION ${STEMWIN_LIB})
target_link_libraries(${PROJECT_NAME}.elf PRIVATE
  -Wl,--start-group
  stemwin
  -Wl,--end-group
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
