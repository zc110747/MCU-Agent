# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# The Zephyr CMake package config lives at
#   <ZEPHYR_BASE>/share/zephyr-package/cmake/ZephyrConfig.cmake
# A vendored tree has no CMake package registry, so find_package() cannot
# discover it via HINTS/CMAKE_PREFIX_PATH.  Point Zephyr_DIR straight at the
# config directory so the build works whether invoked through `west build` or
# a direct `cmake` call.
#
# ZEPHYR_BASE resolution order:
#   1. explicit environment variable (west build sets it automatically)
#   2. vendored tree at <project>/zephyr/zephyr
#
# Normalize to forward slashes: west injects a Windows path with backslashes
# (E:\...\zephyr\zephyr) into ZEPHYR_BASE, and Zephyr's
# zephyr_package_search.cmake passes it to cmake_parse_arguments(), which
# rejects "\c" as an invalid escape.
if(DEFINED ENV{ZEPHYR_BASE})
  string(REPLACE "\\" "/" ZEPHYR_BASE "$ENV{ZEPHYR_BASE}")
else()
  set(ZEPHYR_BASE "${CMAKE_CURRENT_SOURCE_DIR}/zephyr/zephyr")
endif()

set(Zephyr_DIR "${ZEPHYR_BASE}/share/zephyr-package/cmake"
    CACHE PATH "Directory containing ZephyrConfig.cmake")
list(APPEND CMAKE_PREFIX_PATH "${Zephyr_DIR}")

# ---------------------------------------------------------------------------
# Toolchain auto-selection (Zephyr needs this BEFORE find_package(Zephyr)).
# Default to the GNU Arm Embedded toolchain (gnuarmemb) and derive its root
# from arm-none-eabi-gcc found on PATH, so a fresh `west build` works with
# ZERO manual environment setup (west CLI / build_oneclick.bat / VSCode task).
# Only fills in the values when the caller has not already set them.
# ---------------------------------------------------------------------------
if(NOT DEFINED ENV{ZEPHYR_TOOLCHAIN_VARIANT})
  set(ENV{ZEPHYR_TOOLCHAIN_VARIANT} gnuarmemb)
endif()

if(NOT DEFINED ENV{GNUARMEMB_TOOLCHAIN_PATH})
  find_program(ARM_GCC_PROG arm-none-eabi-gcc)
  if(ARM_GCC_PROG)
    get_filename_component(ARM_GCC_BIN   "${ARM_GCC_PROG}" DIRECTORY)
    get_filename_component(ARM_GCC_PREFIX "${ARM_GCC_BIN}" DIRECTORY)
    set(ENV{GNUARMEMB_TOOLCHAIN_PATH} "${ARM_GCC_PREFIX}")
  else()
    message(WARNING
      "arm-none-eabi-gcc not found on PATH; set GNUARMEMB_TOOLCHAIN_PATH "
      "manually or install the GNU Arm Embedded toolchain.")
  endif()
endif()

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(zephyr_oled_lvgl)

# Application sources
target_sources(app PRIVATE
    app/main.c
    app/ui.c
    app/shell_cmds.c
    app/stress.c
    bsp/drv_oled_text.c
    bsp/lv_port_font.c
    bsp/ascii_1608_table.c
    bsp/ascii_2412_table.c
)

# Public/private include paths
target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/app
)

# Link with LVGL so the app can register the custom GBK font
set(CONFIG_APP_LINK_WITH_LVGL y)
