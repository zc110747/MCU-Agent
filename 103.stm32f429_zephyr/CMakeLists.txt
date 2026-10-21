# Copyright (c) 2026
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# ---------------------------------------------------------------------------
# ZEPHYR_BASE resolution order:
#   1. explicit environment variable (west build sets it automatically)
#   2. vendored tree at <project>/zephyr/zephyr
# Normalize to forward slashes: west injects a Windows path with backslashes
# and zephyr_package_search.cmake passes it to cmake_parse_arguments(), which
# rejects "\c" as an invalid escape.
# ---------------------------------------------------------------------------
if(DEFINED ENV{ZEPHYR_BASE})
  string(REPLACE "\\" "/" ZEPHYR_BASE "$ENV{ZEPHYR_BASE}")
else()
  set(ZEPHYR_BASE "${CMAKE_CURRENT_SOURCE_DIR}/zephyr/zephyr")
endif()

set(Zephyr_DIR "${ZEPHYR_BASE}/share/zephyr-package/cmake"
    CACHE PATH "Directory containing ZephyrConfig.cmake")
list(APPEND CMAKE_PREFIX_PATH "${Zephyr_DIR}")

# ---------------------------------------------------------------------------
# Project-local board root: boards/stm32f429ig/ holds the custom board
# definition (LQFP176 / 1 MB flash / 25 MHz HSE / RMII + PCF8574 wiring).
# ---------------------------------------------------------------------------
set(BOARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Zephyr board root")

# ---------------------------------------------------------------------------
# Toolchain auto-selection (must happen BEFORE find_package(Zephyr)).
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

# ---------------------------------------------------------------------------
# Python interpreter.
# Zephyr runs Python helpers (scripts/list_boards.py, devicetree, kconfig).
# They need pykwalify / PyYAML / pyelftools, which a bare interpreter may lack,
# so allow the caller to override the auto-detected one:
#     set PYTHON3_EXECUTABLE=C:\path\to\python.exe   (or pass -DPython3_EXECUTABLE=)
# ---------------------------------------------------------------------------
if(DEFINED ENV{PYTHON3_EXECUTABLE})
  set(Python3_EXECUTABLE "$ENV{PYTHON3_EXECUTABLE}")
endif()

find_package(Zephyr REQUIRED HINTS "${ZEPHYR_BASE}")
project(stm32f429_zephyr_web)

# binutils 2.39+ warns about the RWX LOAD segment that Zephyr's default linker
# script produces on Cortex-M. The segment is intentional (flash is executable,
# .data is copied to RAM by the startup code), so silence the warning to keep
# the build clean. Zephyr creates its link targets during find_package(), hence
# the TARGET guards.
foreach(_zephyr_link_target zephyr_pre0 zephyr_final)
  if(TARGET ${_zephyr_link_target})
    target_link_options(${_zephyr_link_target} PRIVATE -Wl,--no-warn-rwx-segments)
  endif()
endforeach()

target_sources(app PRIVATE
    app/main.c
    app/board_io.c
    app/sdram.c
    app/web_server.c
)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
)
