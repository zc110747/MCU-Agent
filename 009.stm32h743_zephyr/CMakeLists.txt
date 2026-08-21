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
if(DEFINED ENV{ZEPHYR_BASE})
  set(ZEPHYR_BASE "$ENV{ZEPHYR_BASE}")
else()
  set(ZEPHYR_BASE "${CMAKE_CURRENT_SOURCE_DIR}/zephyr/zephyr")
endif()

set(Zephyr_DIR "${ZEPHYR_BASE}/share/zephyr-package/cmake"
    CACHE PATH "Directory containing ZephyrConfig.cmake")
list(APPEND CMAKE_PREFIX_PATH "${Zephyr_DIR}")

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(zephyr_oled_lvgl)

# Application sources
target_sources(app PRIVATE
    app/main.c
    app/ui.c
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
