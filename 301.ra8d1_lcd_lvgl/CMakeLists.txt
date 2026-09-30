# 301.ra8d1_lcd_lvgl - RA8D1 Vision Board bare-metal + RT-Thread Nano + LVGL
cmake_minimum_required(VERSION 3.24)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm_gcc.cmake" CACHE STRING "Arm GNU toolchain")

# ASM is required: RT-Thread's cortex-m85 port ships context_gcc.S
project(ra8d1_lcd_lvgl C ASM)

set(PROJ_ROOT "${CMAKE_CURRENT_LIST_DIR}")

# ---------------------------------------------------------------- self code
file(GLOB SELF_SOURCES
    "${PROJ_ROOT}/applications/*.c"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/*.c"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/gen/*.c"
)

# ------------------------------------------------------- upstream FSP (as-is)
file(GLOB FSP_BSP_SOURCES
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/bsp/cmsis/Device/RENESAS/Source/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/bsp/mcu/all/*.c"
)

# Upstream FSP peripheral modules (added per Phase, kept as-is).
# These are plain APIs called directly from our code, so a static library is
# fine here: archive extraction is driven by undefined symbols.
file(GLOB FSP_MODULE_SOURCES
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_ioport/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_sci_b_uart/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_glcdc/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_ceu/*.c"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/r_gpt/*.c"
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
# .rti_fn table) is silently dropped: __rt_init_rti_end ends up equal to
# __rt_init_rti_start + 4 markers and finsh never starts - no warning, no
# error, just a dead console.
#
# Compiling the sources straight into the executable keeps the section tables,
# and --gc-sections still trims the code RT-Thread does not use.
file(GLOB RTTHREAD_C_SOURCES
    "${PROJ_ROOT}/third_party/rt-thread-nano/src/*.c"
    "${PROJ_ROOT}/third_party/rt-thread-nano/components/finsh/*.c"
    "${PROJ_ROOT}/third_party/rt-thread-nano/libcpu/arm/cortex-m85/*.c"
)
file(GLOB RTTHREAD_ASM_SOURCES
    "${PROJ_ROOT}/third_party/rt-thread-nano/libcpu/arm/cortex-m85/*.S"
)

set(COMMON_INCLUDES
    "${PROJ_ROOT}/applications"
    "${PROJ_ROOT}/bsp"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/gen"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/cfg"
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

target_link_libraries(firmware PRIVATE fsp_modules)

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
