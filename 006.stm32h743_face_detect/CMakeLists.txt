cmake_minimum_required(VERSION 3.22)

# The toolchain file must be set before project(); allow the usual
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake
# but default to it so a bare `cmake -B build -G Ninja` also works.
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain-arm-none-eabi.cmake"
        CACHE FILEPATH "toolchain file")
endif()

project(stm32h7_face_detect C ASM)
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
    -Wno-sign-compare
    -ffunction-sections
    -fdata-sections
    -fno-common
)

add_compile_options($<$<CONFIG:Debug>:-Og>          $<$<CONFIG:Debug>:-g3>)
add_compile_options($<$<CONFIG:Release>:-O2>        $<$<CONFIG:Release>:-g>)
add_compile_options($<$<CONFIG:RelWithDebInfo>:-O2> $<$<CONFIG:RelWithDebInfo>:-g3>)

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
set(PROJECT_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/face_detect
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
# Only the HAL modules this board actually uses are compiled.  A full glob
# would pull in 125 files and slow every rebuild down for no benefit; the set
# below must stay in sync with Core/Inc/stm32h7xx_hal_conf.h.
set(HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src)
set(HAL_SOURCES
    ${HAL_DIR}/stm32h7xx_hal.c
    ${HAL_DIR}/stm32h7xx_hal_cortex.c
    ${HAL_DIR}/stm32h7xx_hal_dma.c
    ${HAL_DIR}/stm32h7xx_hal_dma_ex.c
    ${HAL_DIR}/stm32h7xx_hal_mdma.c
    ${HAL_DIR}/stm32h7xx_hal_exti.c
    ${HAL_DIR}/stm32h7xx_hal_flash.c
    ${HAL_DIR}/stm32h7xx_hal_flash_ex.c
    ${HAL_DIR}/stm32h7xx_hal_gpio.c
    ${HAL_DIR}/stm32h7xx_hal_hsem.c
    ${HAL_DIR}/stm32h7xx_hal_pwr.c
    ${HAL_DIR}/stm32h7xx_hal_pwr_ex.c
    ${HAL_DIR}/stm32h7xx_hal_rcc.c
    ${HAL_DIR}/stm32h7xx_hal_rcc_ex.c
    ${HAL_DIR}/stm32h7xx_hal_dcmi.c
    ${HAL_DIR}/stm32h7xx_hal_i2c.c
    ${HAL_DIR}/stm32h7xx_hal_i2c_ex.c
    ${HAL_DIR}/stm32h7xx_hal_spi.c
    ${HAL_DIR}/stm32h7xx_hal_spi_ex.c
    ${HAL_DIR}/stm32h7xx_hal_uart.c
    ${HAL_DIR}/stm32h7xx_hal_uart_ex.c
)

file(GLOB_RECURSE CMSIS_NN_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/NN/Source/*.c)

set(PROJECT_SOURCES
    sys_startup/gcc/startup_stm32h743xx.s

    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/stm32h7xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/stm32h7xx_hal_msp.c
    sys_startup/system_stm32h7xx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/syscalls.c

    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/drv_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/drv_spi_oled.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/drv_oled_fonts.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/drv_dcmi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/drv_dcmi_ov5640.c

    # Face-detection middleware (CMSIS-NN CenterNet-style detector, 96x96 int8)
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/face_detect/fd_model_data.c
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/face_detect/fd_infer.c

    ${CMAKE_CURRENT_SOURCE_DIR}/app/app_face.c
)

add_executable(${TARGET_NAME}
    ${PROJECT_SOURCES}
    ${HAL_SOURCES}
    ${CMSIS_NN_SOURCES}
)

target_include_directories(${TARGET_NAME} PRIVATE ${PROJECT_INCLUDES})

# CMSIS-NN is pure compute: always optimise hard, even in a debug build, or
# a single inference takes seconds instead of tens of milliseconds.
set_source_files_properties(${CMSIS_NN_SOURCES} PROPERTIES COMPILE_OPTIONS "-O3")

# The model blob is a huge const table - never worth compiling with -Og.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/middleware/face_detect/fd_model_data.c
    PROPERTIES COMPILE_OPTIONS "-O2")

# HAL and the generated font tables are third-party: silence their warnings so
# ours stay visible.
set_source_files_properties(${HAL_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/bsp/drv_oled_fonts.c
    PROPERTIES COMPILE_OPTIONS "-w")

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
