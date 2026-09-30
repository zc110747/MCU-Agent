cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
#  STM32H743ZIT6 - SD card JPEG slideshow on a 240x240 SPI OLED
#
#  Configure / build (toolchain and generator come from CMakePresets.json):
#      cmake --preset debug
#      cmake --build --preset debug
# ---------------------------------------------------------------------------

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(stm32_sd_oled C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(TARGET_NAME ${PROJECT_NAME})
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/sys_startup/STM32H743ZITX_FLASH.ld)

# ---------------------------------------------------------------------------
#  Sources
# ---------------------------------------------------------------------------

# HAL: import ONLY the modules this project actually uses (do not glob the whole Src dir).
# Reverse-looked-up from the HAL_*() calls in Core/ bsp/ app/:
#   GPIO PWR RCC SD SPI TIM UART EXTXRCC -> see list below.
# The 5 mandatory core modules (hal / hal_cortex / hal_rcc / hal_rcc_ex / hal_gpio) are always required.
# hal_dma(+_ex) is required because hal_uart.c / hal_spi.c / hal_tim.c reference HAL_DMA_* symbols.
# hal_exti is required because hal_pwr_ex.c references HAL_EXTI_Dx_EventInputConfig().
# ll_sdmmc.c is a required companion of hal_sd.c (register-level SDMMC layer).
set(HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src)
set(HAL_SOURCES
    # --- mandatory core ---
    ${HAL_DIR}/stm32h7xx_hal.c
    ${HAL_DIR}/stm32h7xx_hal_cortex.c
    ${HAL_DIR}/stm32h7xx_hal_rcc.c
    ${HAL_DIR}/stm32h7xx_hal_rcc_ex.c
    ${HAL_DIR}/stm32h7xx_hal_gpio.c
    # --- power + exti ---
    ${HAL_DIR}/stm32h7xx_hal_pwr.c
    ${HAL_DIR}/stm32h7xx_hal_pwr_ex.c
    ${HAL_DIR}/stm32h7xx_hal_exti.c
    # --- dma (referenced by uart/spi/tim drivers) ---
    ${HAL_DIR}/stm32h7xx_hal_dma.c
    ${HAL_DIR}/stm32h7xx_hal_dma_ex.c
    # --- peripherals actually used: SD (polling) / SPI / TIM / UART ---
    ${HAL_DIR}/stm32h7xx_hal_sd.c
    ${HAL_DIR}/stm32h7xx_ll_sdmmc.c
    ${HAL_DIR}/stm32h7xx_hal_spi.c
    ${HAL_DIR}/stm32h7xx_hal_tim.c
    ${HAL_DIR}/stm32h7xx_hal_tim_ex.c
    ${HAL_DIR}/stm32h7xx_hal_uart.c
    ${HAL_DIR}/stm32h7xx_hal_uart_ex.c
)

file(GLOB CORE_SOURCES  CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/*.c)
file(GLOB BSP_SOURCES   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bsp/*.c)
file(GLOB APP_SOURCES   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/app/*.c)
file(GLOB FatFs_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/FatFs/*.c)
file(GLOB TJPGD_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tjpgd/*.c)

set(STARTUP_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/sys_startup/gcc/startup_stm32h743xx.s)
set(SYSTEM_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/sys_startup/system_stm32h7xx.c)

add_executable(${TARGET_NAME}
    ${CORE_SOURCES}
    ${BSP_SOURCES}
    ${APP_SOURCES}
    ${HAL_SOURCES}
    ${FatFs_SOURCES}
    ${TJPGD_SOURCES}
    ${STARTUP_SOURCE}
    ${SYSTEM_SOURCE}
)

# ---------------------------------------------------------------------------
#  Include paths
# ---------------------------------------------------------------------------

target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    sys_startup
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/FatFs
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tjpgd
)

# ---------------------------------------------------------------------------
#  Defines
# ---------------------------------------------------------------------------

target_compile_definitions(${TARGET_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32H743xx
    $<$<CONFIG:Debug>:DEBUG>
)

# ---------------------------------------------------------------------------
#  Compile options
# ---------------------------------------------------------------------------

target_compile_options(${TARGET_NAME} PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard

    -Wall
    -Wno-unused-parameter

    -fdata-sections
    -ffunction-sections
    -fno-common

    # Vendor HAL and the OLED driver type-pun volatile MMIO registers (writing
    # 16-bit halves into the 32-bit SPI TXDR to pack the FIFO). That is the
    # hardware-mandated access pattern, so tell GCC not to assume otherwise -
    # without this, -O2 is free to reorder those accesses.
    -fno-strict-aliasing

    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-gdwarf-4>
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-g>

    $<$<COMPILE_LANGUAGE:ASM>:-x$<SEMICOLON>assembler-with-cpp>
)

# ---------------------------------------------------------------------------
#  Link options
# ---------------------------------------------------------------------------

target_link_options(${TARGET_NAME} PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfpu=fpv5-d16
    -mfloat-abi=hard

    -T${LINKER_SCRIPT}
    --specs=nano.specs
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    -Wl,-Map=${TARGET_NAME}.map,--cref
    -Wl,--no-warn-rwx-segments
    -static
)

target_link_libraries(${TARGET_NAME} PRIVATE c m)

# Relink whenever the linker script changes
set_target_properties(${TARGET_NAME} PROPERTIES
    SUFFIX ".elf"
    LINK_DEPENDS ${LINKER_SCRIPT}
)

# ---------------------------------------------------------------------------
#  Post build: size report + hex/bin images
# ---------------------------------------------------------------------------

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${TARGET_NAME}>
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.bin
    COMMENT "Building ${TARGET_NAME}.hex / ${TARGET_NAME}.bin"
)
