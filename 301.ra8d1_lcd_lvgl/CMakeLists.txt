# 301.ra8d1_lcd_lvgl - RA8D1 Vision Board bare-metal + RT-Thread Nano + LVGL
cmake_minimum_required(VERSION 3.24)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm_gcc.cmake" CACHE STRING "Arm GNU toolchain")

project(ra8d1_lcd_lvgl C)

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

set(COMMON_INCLUDES
    "${PROJ_ROOT}/applications"
    "${PROJ_ROOT}/bsp"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/gen"
    "${PROJ_ROOT}/bsp/ra8d1-vision-board/cfg"
    "${PROJ_ROOT}/Drivers/renesas/fsp/inc"
    "${PROJ_ROOT}/Drivers/renesas/fsp/inc/api"
    "${PROJ_ROOT}/Drivers/renesas/fsp/inc/instances"
    "${PROJ_ROOT}/Drivers/renesas/fsp/src/bsp"
    "${PROJ_ROOT}/Drivers/renesas"
    "${PROJ_ROOT}/Drivers/renesas/arm/CMSIS_5/CMSIS/Core/Include"
)

add_executable(firmware ${SELF_SOURCES} ${FSP_BSP_SOURCES})

set_target_properties(firmware PROPERTIES
    LINKER_LANGUAGE C
    SUFFIX ".elf"
)

target_include_directories(firmware PRIVATE ${COMMON_INCLUDES})

# strict warnings for self-written code only
target_compile_options(firmware PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra>
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
