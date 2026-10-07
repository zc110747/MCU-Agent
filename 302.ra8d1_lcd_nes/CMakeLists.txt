# 302.ra8d1_lcd_nes - RA8D1 Vision Board NES emulator + DRW2D + W25Q64 FatFs + USB MSC
#
# Layering (repo convention):
#   applications/            our app code            -Wall -Wextra, zero warnings
#   bsp/ra8d1-vision-board/  our board support       -Wall -Wextra, zero warnings
#   bsp/nes/                 our NES port layer      -Wall -Wextra, zero warnings
#   Drivers/renesas/fsp/     upstream FSP            -w (never fork upstream)
#   third_party/{rt-thread-nano,dave2d,nes,tinyusb}  upstream, -w
cmake_minimum_required(VERSION 3.24)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm_gcc.cmake" CACHE STRING "Arm GNU toolchain")

# ASM is required: RT-Thread's cortex-m85 port ships context_gcc.S
project(ra8d1_lcd_nes C ASM)

set(PROJ_ROOT "${CMAKE_CURRENT_LIST_DIR}")

# ------------------------------------------------------------ optimisation
#
# Without this block GCC defaults to -O0, which is not a usable configuration:
# the NES opcode interpreter and the per-scanline PPU renderers are memory-heavy
# inner loops that an unoptimised build leaves far too slow.  The upstream
# reference project is built with the MDK optimiser on, so -O0 is not
# like-for-like either.
#
# KNOWN ISSUE - -O1 and -O2 break the console
# ------------------------------------------
# With -O1 or -O2 the serial console emits truncated lines: every
# rt_kprintf("[heartbeat] tick=%u ...") stops dead after the literal "tick=",
# so the %u is never formatted and no command response is readable.  The same
# image with -Og is perfect, and the reference project builds fine at higher
# optimisation on MDK, so the trigger is this toolchain's handling of RT-Thread's
# varargs formatting under a mid-level optimiser - not the emulator code.
# It is not yet diagnosed; until it is, -Og is used for both presets so that the
# Debug and Release binaries differ only in debug info, not in behaviour.
#
# Measured with -Og (480 MHz core, 8,000,000 cycles = one 60 Hz frame):
#   present path   0.61 M cyc/frame   (  7% of budget)
#   emulation     27.0  M cyc/frame   (337% of budget)  -> ~13 fps
# See documents/ for the profiling record.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_C_FLAGS_DEBUG          "-Og -g3 -gdwarf-4")
set(CMAKE_C_FLAGS_RELEASE        "-Og -g0 -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Og -g3 -gdwarf-4 -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL     "-Og -g0 -DNDEBUG")

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ---------------------------------------------------------------- self code
file(GLOB SELF_SOURCES
    "${PROJ_ROOT}/applications/*.c"
    "${PROJ_ROOT}/applications/ui/*.c"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/*.c"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/gen/*.c"
    "${PROJ_ROOT}/bsp/nes/*.c"
)

# ------------------------------------------------------- upstream FSP (as-is)
file(GLOB FSP_BSP_SOURCES
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/bsp/cmsis/Device/RENESAS/Source/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/bsp/mcu/all/*.c"
)

# Upstream FSP peripheral modules. Plain APIs called from our code, so a static
# library is fine: archive extraction is driven by undefined symbols.
file(GLOB FSP_MODULE_SOURCES
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_ioport/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_sci_b_uart/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_glcdc/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_gpt/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_mipi_phy/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_mipi_dsi/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_ospi_b/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_drw/*.c"
)

# Renesas EK board support (SDRAM controller init sequence), upstream as-is.
file(GLOB FSP_BOARD_SOURCES
    "${PROJ_ROOT}/Drivers/renesas/fsp/board/ra8d1_ek/*.c"
)

# ------------------------------------------------- upstream RT-Thread Nano
#
# NOT built as a static library, on purpose.
#
# RT-Thread registers components through linker *section tables*, not through
# symbols: INIT_APP_EXPORT(finsh_system_init) emits a const function pointer
# into ".rti_fn.6", and MSH_CMD_EXPORT emits into "FSymTab". A linker only
# pulls an archive member when that member resolves an undefined *symbol*, so
# with rt-thread in a .a the shell.c member (whose only entry point is the
# .rti_fn table) is silently dropped: finsh never starts - no warning, no
# error, just a dead console.
file(GLOB RTTHREAD_C_SOURCES
    "${PROJ_ROOT}/third_party/rt-thread-nano/src/*.c"
    "${PROJ_ROOT}/third_party/rt-thread-nano/components/finsh/*.c"
    "${PROJ_ROOT}/third_party/rt-thread-nano/libcpu/arm/cortex-m85/*.c"
)
file(GLOB RTTHREAD_ASM_SOURCES
    "${PROJ_ROOT}/third_party/rt-thread-nano/libcpu/arm/cortex-m85/*.S"
)

# ------------------------------------------------- upstream NES core (as-is)
# Plain API calls only, no section-table registration -> static library is safe.
file(GLOB NES_CORE_SOURCES
    "${PROJ_ROOT}/third_party/nes/src/*.c"
    "${PROJ_ROOT}/third_party/nes/src/nes_mapper/*.c"
)

# ------------------------------------------------- upstream DRW2D (as-is)
# Renesas 2D drawing engine (TES Dave2D). The FSP r_drw module calls d2_* entry
# points, so these are resolved as undefined symbols -> a static library works.
file(GLOB DAVE2D_SOURCES
    "${PROJ_ROOT}/third_party/dave2d/src/*.c"
)

add_library(dave2d STATIC ${DAVE2D_SOURCES})
target_include_directories(dave2d PRIVATE
    "${PROJ_ROOT}/third_party/dave2d/inc"
    "${PROJ_ROOT}/third_party/dave2d/src"
    "${PROJ_ROOT}/Drivers/renesas/fsp/inc"
    "${PROJ_ROOT}/Drivers/renesas/fsp/inc/api"
    "${PROJ_ROOT}/Drivers/renesas/fsp/inc/instances"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/bsp"
    "${PROJ_ROOT}/Drivers/renesas"
    "${PROJ_ROOT}/Drivers/renesas/arm/CMSIS_5/CMSIS/Core/Include"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/cfg"
)
target_compile_options(dave2d PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)

set(COMMON_INCLUDES
    "${PROJ_ROOT}/applications"
    "${PROJ_ROOT}/applications/ui"
    "${PROJ_ROOT}/bsp"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/gen"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/cfg"
    "${PROJ_ROOT}/bsp/nes"
    "${PROJ_ROOT}/third_party/nes/inc"
    "${PROJ_ROOT}/third_party/dave2d/inc"
    "${PROJ_ROOT}/third_party/dave2d/src"
    "${PROJ_ROOT}/Drivers/renesas/fsp/inc"
    "${PROJ_ROOT}/Drivers/renesas/fsp/inc/api"
    "${PROJ_ROOT}/Drivers/renesas/fsp/inc/instances"
    "${PROJ_ROOT}/Drivers/renesas/fsp/board/ra8d1_ek"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/bsp"
    "${PROJ_ROOT}/Drivers/renesas"
    "${PROJ_ROOT}/Drivers/renesas/arm/CMSIS_5/CMSIS/Core/Include"
)

set(RTTHREAD_INCLUDES
    "${PROJ_ROOT}/third_party/rt-thread-nano/include"
    "${PROJ_ROOT}/third_party/rt-thread-nano/components/finsh"
    "${PROJ_ROOT}/third_party/rt-thread-nano/libcpu/arm/cortex-m85"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/cfg"          # rtconfig.h
)

# Upstream FSP peripheral modules: static lib, warnings off.
# The zero-warning rule applies to code we own; "fixing" upstream would mean
# forking it.
add_library(fsp_modules STATIC ${FSP_MODULE_SOURCES} ${FSP_BOARD_SOURCES})
target_include_directories(fsp_modules PRIVATE
    ${COMMON_INCLUDES}
    "${PROJ_ROOT}/Drivers/renesas/fsp/board/ra8d1_ek"
)
target_compile_options(fsp_modules PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)

# Upstream NES core: static lib, warnings off.
add_library(nes_core STATIC ${NES_CORE_SOURCES})
target_include_directories(nes_core PRIVATE
    "${PROJ_ROOT}/third_party/nes/inc"
    "${PROJ_ROOT}/bsp/nes"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/cfg"
    "${PROJ_ROOT}/third_party/rt-thread-nano/include"
    "${PROJ_ROOT}/third_party/rt-thread-nano/components/finsh"
    "${PROJ_ROOT}/Drivers/renesas/fsp/inc"
    "${PROJ_ROOT}/Drivers/renesas/fsp/inc/api"
    "${PROJ_ROOT}/Drivers/renesas"
    "${PROJ_ROOT}/Drivers/renesas/arm/CMSIS_5/CMSIS/Core/Include"
)
target_compile_options(nes_core PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)

add_executable(firmware
    ${SELF_SOURCES}
    ${FSP_BSP_SOURCES}
    ${RTTHREAD_C_SOURCES}
    ${RTTHREAD_ASM_SOURCES}
)

set_target_properties(firmware PROPERTIES
    LINKER_LANGUAGE C
    SUFFIX ".elf"
)

target_include_directories(firmware PRIVATE
    ${COMMON_INCLUDES}
    ${RTTHREAD_INCLUDES}
)

# Upstream RT-Thread sources: warnings off, same reasoning as fsp_modules.
set_source_files_properties(${RTTHREAD_C_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

# dave2d (libdave2d.a) calls the d1_* hardware layer that lives in
# r_drw_base.c, which is inside libfsp_modules.a -- a circular dependency
# between two archives. A single left-to-right pass would drop whichever
# archive is scanned first, so group them.
target_link_libraries(firmware PRIVATE
    -Wl,--start-group fsp_modules nes_core dave2d -Wl,--end-group
)

# strict warnings for self-written code only.
# -Wno-cast-function-type: finsh's MSH_CMD_EXPORT macro casts every command
# function to syscall_func (long (*)(void)). That is an upstream macro, so the
# warning is suppressed here rather than forking finsh.h.
target_compile_options(firmware PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wno-cast-function-type>
)

target_link_options(firmware PRIVATE
    "-T${PROJ_ROOT}/bsp/ra8d1-vision-board/linker/fsp.ld"
    "-L${PROJ_ROOT}/bsp/ra8d1-vision-board/linker"
    "-Wl,-Map=firmware.map"
    "--specs=nano.specs"
    "--specs=nosys.specs"
    "-Wl,--gc-sections"
    "-Wl,--cref"
)

find_program(SIZE_EXEC arm-none-eabi-size REQUIRED)
find_program(OBJCOPY_EXEC arm-none-eabi-objcopy REQUIRED)

add_custom_command(TARGET firmware POST_BUILD
    COMMAND ${OBJCOPY_EXEC} -O binary "$<TARGET_FILE:firmware>" firmware.bin
    COMMAND ${OBJCOPY_EXEC} -O ihex "$<TARGET_FILE:firmware>" firmware.hex
    COMMAND ${SIZE_EXEC} "$<TARGET_FILE:firmware>" > size.txt
    COMMAND ${SIZE_EXEC} "$<TARGET_FILE:firmware>"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:firmware>"
    VERBATIM
)
