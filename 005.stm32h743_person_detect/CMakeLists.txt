cmake_minimum_required(VERSION 3.22)

# The toolchain file must be set before project(); allow the usual
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake
# but default to it so a bare `cmake -B build` also works.
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-arm-none-eabi.cmake"
        CACHE FILEPATH "toolchain file")
endif()

project(stm32h7_person_detect C ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

set(TARGET_NAME ${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Global definitions
# ---------------------------------------------------------------------------
add_compile_definitions(
    USE_HAL_DRIVER
    STM32H743xx
    ARM_MATH_CM7
    __FPU_PRESENT=1U
)

# ---------------------------------------------------------------------------
# Compile options
# ---------------------------------------------------------------------------
add_compile_options(
    -Wall
    -Wextra
    -Wno-unused-parameter
    -ffunction-sections
    -fdata-sections
    -fno-common
    -fstack-usage
    -Wno-sign-compare
)

# -O2 even in Debug: the CNN is far too slow at -Og and the debug experience
# is still fine because -g3 keeps full symbol information.
add_compile_options($<$<CONFIG:Debug>:-Og> $<$<CONFIG:Debug>:-g3>)
add_compile_options($<$<CONFIG:Release>:-O2> $<$<CONFIG:Release>:-g>)
add_compile_options($<$<CONFIG:RelWithDebInfo>:-O2> $<$<CONFIG:RelWithDebInfo>:-g3>)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
set(PROJECT_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/person_detect
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    sys_startup
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Core/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/DSP/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/NN/Include
)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
file(GLOB HAL_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/*.c)
list(FILTER HAL_SOURCES EXCLUDE REGEX ".*_template\\.c$")

file(GLOB_RECURSE CMSIS_NN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/NN/Source/*.c)

set(PROJECT_SOURCES
    sys_startup/gcc/startup_stm32h743xx.s

    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/stm32h7xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/stm32h7xx_hal_msp.c
    sys_startup/system_stm32h7xx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/syscalls.c

    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/drv_spi_oled.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/drv_oled_fonts.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/drv_dcmi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/drv_dcmi_ov5640.c

    # Person-detection middleware (CMSIS-NN MobileNetV1-0.25, 96x96 int8)
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/person_detect/pd_model_data.c
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/person_detect/pd_infer.c

    ${CMAKE_CURRENT_SOURCE_DIR}/app/app_vision.c
)

add_executable(${TARGET_NAME}
    ${PROJECT_SOURCES}
    ${HAL_SOURCES}
    ${CMSIS_NN_SOURCES}
)

target_include_directories(${TARGET_NAME} PRIVATE ${PROJECT_INCLUDES})

# CMSIS-NN is pure compute: always optimise hard.
set_source_files_properties(${CMSIS_NN_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-O3")

# HAL is third-party: silence its warnings so ours stay visible.
set_source_files_properties(${HAL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

# ---------------------------------------------------------------------------
# Link
# ---------------------------------------------------------------------------
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/sys_startup/STM32H743ZITx_FLASH.ld)

target_link_options(${TARGET_NAME} PRIVATE
    -T${LINKER_SCRIPT}
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${CMAKE_BINARY_DIR}/${TARGET_NAME}.map,--cref
    -Wl,--no-warn-rwx-segments
    -u _printf_float
)

set_target_properties(${TARGET_NAME} PROPERTIES
    SUFFIX ".elf"
    LINK_DEPENDS ${LINKER_SCRIPT})

target_link_libraries(${TARGET_NAME} PRIVATE c m nosys)

# ---------------------------------------------------------------------------
# Post build: hex / bin / size
# ---------------------------------------------------------------------------
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET_NAME}>
            ${CMAKE_BINARY_DIR}/${TARGET_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}>
            ${CMAKE_BINARY_DIR}/${TARGET_NAME}.bin
    COMMENT "Generating .hex / .bin and printing size")
