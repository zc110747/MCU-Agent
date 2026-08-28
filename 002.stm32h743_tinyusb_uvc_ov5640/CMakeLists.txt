cmake_minimum_required(VERSION 3.20)

# The toolchain file must be set before project(). Allow the user to pass
# -DCMAKE_TOOLCHAIN_FILE=... but default to the bundled one.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-none-eabi-gcc.cmake"
        CACHE FILEPATH "Toolchain file")
endif()

project(stm32h743_uvc
        VERSION 1.0.0
        LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(TARGET_NAME ${PROJECT_NAME})

# --------------------------------------------------------------------------
# Paths
# --------------------------------------------------------------------------
set(HAL_DIR      ${CMAKE_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver)
set(CMSIS_DIR    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS)
set(TINYUSB_DIR  ${CMAKE_SOURCE_DIR}/third_party/tinyusb)
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/sys_startup/STM32H743ZITx_FLASH.ld)

# --------------------------------------------------------------------------
# Sources
# --------------------------------------------------------------------------
set(APP_SOURCES
    Core/Src/main.c
    sys_startup/system_stm32h7xx.c
    Core/Src/stm32h7xx_it.c
    Core/Src/stm32h7xx_hal_msp.c
    Core/Src/bsp_board.c
    Core/Src/bsp_camera.c
    Core/Src/ov5640_ref.c
    Core/Src/usb_descriptors.c
    Core/Src/uvc_app.c
    Core/Src/syscalls.c
)

set(OV5640_SOURCES
    BSP/ov5640/ov5640.c
    BSP/ov5640/ov5640_reg.c
)

set(HAL_SOURCES
    ${HAL_DIR}/Src/stm32h7xx_hal.c
    ${HAL_DIR}/Src/stm32h7xx_hal_cortex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_dcmi.c
    ${HAL_DIR}/Src/stm32h7xx_hal_dma.c
    ${HAL_DIR}/Src/stm32h7xx_hal_dma_ex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_mdma.c
    ${HAL_DIR}/Src/stm32h7xx_hal_exti.c
    ${HAL_DIR}/Src/stm32h7xx_hal_flash.c
    ${HAL_DIR}/Src/stm32h7xx_hal_flash_ex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_gpio.c
    ${HAL_DIR}/Src/stm32h7xx_hal_hsem.c
    ${HAL_DIR}/Src/stm32h7xx_hal_i2c.c
    ${HAL_DIR}/Src/stm32h7xx_hal_i2c_ex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_pwr.c
    ${HAL_DIR}/Src/stm32h7xx_hal_pwr_ex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_rcc.c
    ${HAL_DIR}/Src/stm32h7xx_hal_rcc_ex.c
    ${HAL_DIR}/Src/stm32h7xx_hal_uart.c
    ${HAL_DIR}/Src/stm32h7xx_hal_uart_ex.c
)

set(TINYUSB_SOURCES
    ${TINYUSB_DIR}/src/tusb.c
    ${TINYUSB_DIR}/src/common/tusb_fifo.c
    ${TINYUSB_DIR}/src/device/usbd.c
    ${TINYUSB_DIR}/src/device/usbd_control.c
    ${TINYUSB_DIR}/src/class/video/video_device.c
    ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dcd_dwc2.c
    ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dwc2_common.c
)
# usbd_control.c was merged into usbd.c in newer TinyUSB revisions
foreach(_src ${TINYUSB_SOURCES})
    if(NOT EXISTS ${_src})
        list(REMOVE_ITEM TINYUSB_SOURCES ${_src})
    endif()
endforeach()

set(STARTUP_SOURCE sys_startup/gcc/startup_stm32h743xx.s)

add_executable(${TARGET_NAME}
    ${APP_SOURCES}
    ${OV5640_SOURCES}
    ${HAL_SOURCES}
    ${TINYUSB_SOURCES}
    ${STARTUP_SOURCE}
)

# --------------------------------------------------------------------------
# Include directories
# --------------------------------------------------------------------------
target_include_directories(${TARGET_NAME} PRIVATE
    Core/Inc
    BSP/ov5640
    ${HAL_DIR}/Inc
    ${HAL_DIR}/Inc/Legacy
    ${CMSIS_DIR}/Include
    sys_startup
    ${TINYUSB_DIR}/src
)

# --------------------------------------------------------------------------
# Compile definitions
# --------------------------------------------------------------------------
target_compile_definitions(${TARGET_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32H743xx
    HSE_VALUE=25000000
    # ---- TinyUSB ----
    CFG_TUSB_MCU=OPT_MCU_STM32H7
    CFG_TUSB_OS=OPT_OS_NONE
    BOARD_TUD_RHPORT=0
    BOARD_TUD_MAX_SPEED=OPT_MODE_FULL_SPEED
)

# --------------------------------------------------------------------------
# Compile options
# --------------------------------------------------------------------------
target_compile_options(${TARGET_NAME} PRIVATE
    -Wall
    -Wextra
    -fdata-sections
    -ffunction-sections
    -fno-common
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Debug>:-gdwarf-4>
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Release>:-g0>
    $<$<CONFIG:RelWithDebInfo>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-g3>
    $<$<CONFIG:RelWithDebInfo>:-gdwarf-4>
)

# Third-party code should not fail the build on style warnings
set_source_files_properties(${HAL_SOURCES} ${OV5640_SOURCES} ${TINYUSB_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare")

# --------------------------------------------------------------------------
# Link options
# --------------------------------------------------------------------------
target_link_options(${TARGET_NAME} PRIVATE
    -T${LINKER_SCRIPT}
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${TARGET_NAME}.map,--cref
    -Wl,--print-memory-usage
    # DTCM is declared executable (xrw) so .data may hold RAM functions.
    -Wl,--no-warn-rwx-segments
    -u _printf_float
)
set_target_properties(${TARGET_NAME} PROPERTIES
    SUFFIX ".elf"
    LINK_DEPENDS ${LINKER_SCRIPT}
)
target_link_libraries(${TARGET_NAME} PRIVATE c m nosys)

# --------------------------------------------------------------------------
# Post build: hex / bin / size
# --------------------------------------------------------------------------
add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.bin
    COMMENT "Generating ${TARGET_NAME}.hex / ${TARGET_NAME}.bin"
)

# --------------------------------------------------------------------------
# Convenience targets: flash / erase via OpenOCD
# --------------------------------------------------------------------------
find_program(OPENOCD_EXECUTABLE openocd)
if(OPENOCD_EXECUTABLE)
    add_custom_target(flash
        COMMAND ${OPENOCD_EXECUTABLE}
                -f ${CMAKE_SOURCE_DIR}/debug/openocd.cfg
                -c "program $<TARGET_FILE:${TARGET_NAME}> verify reset exit"
        DEPENDS ${TARGET_NAME}
        COMMENT "Flashing ${TARGET_NAME}.elf via ST-Link")
endif()
