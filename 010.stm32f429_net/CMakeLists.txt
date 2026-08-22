cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/arm-none-eabi-gcc.cmake CACHE FILEPATH "" FORCE)
project(stm32f429_net C ASM)

# ------------------------------------------------------------------
# MCU / toolchain flags
# ------------------------------------------------------------------
set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # -> -std=gnu99 (allows // comments, GNU extensions)

# C-only flags. (add_compile_options would also hit the assembler, so we
# scope these to C via CMAKE_C_FLAGS.)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MCU_FLAGS} -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -fdata-sections -ffunction-sections -g -Og")

# Assembler flags for the GNU startup file.
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${MCU_FLAGS} -x assembler-with-cpp -g -Og")

# Linker flags: flash linker script, map, dead-code strip, newlib-nano.
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx/Source/STM32F429IGTx_FLASH.ld)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MCU_FLAGS} -T${LINKER_SCRIPT} -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map -Wl,--gc-sections -Wl,--print-memory-usage -specs=nano.specs -lc -lm -lgcc")

# ------------------------------------------------------------------
# Include paths (order matters: app/lwip must win over the bundled
# third_party/LwIP/system/arch so our NO_SYS cc.h is used).
# ------------------------------------------------------------------
include_directories(
  app
  app/lwip
  app/web
  bsp
  Drivers/CMSIS/Include
  Drivers/STM32F4xx_HAL_Driver/Inc
  third_party/LwIP/src/include
  third_party/LwIP/system/arch
)

add_compile_definitions(STM32F429xx USE_HAL_DRIVER)

# ------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------
file(GLOB APP_SOURCES
  app/*.c
  app/lwip/*.c
  app/web/*.c
  bsp/*.c
)

set(CMSIS_SOURCES
  Drivers/CMSIS/Device/ST/STM32F4xx/Source/system_stm32f4xx.c
  Drivers/CMSIS/Device/ST/STM32F4xx/Source/GCC/startup_stm32f429xx.s
)

# Only the HAL modules we actually use (their HAL_*_MODULE_ENABLED are set
# in app/stm32f4xx_hal_conf.h, so each .c compiles cleanly).
set(HAL_SOURCES
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_eth.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
)

# LwIP core for a NO_SYS, IPv4, static-IP build. Exclude the OS/API/PPP/IPv6
# and template files that require an RTOS or extra subsystems.
file(GLOB_RECURSE LWIP_SOURCES ${CMAKE_SOURCE_DIR}/third_party/LwIP/src/*.c)
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/api/.*")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/apps/.*")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/ppp/.*")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/ipv6/.*")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/netif/lowpan6.*")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/netif/bridgeif.*")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/netif/slipif\\.c")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/netif/zepif\\.c")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/core/sys\\.c")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/core/altcp.*\\.c")

# ------------------------------------------------------------------
# Executable
# ------------------------------------------------------------------
set(CMAKE_EXECUTABLE_SUFFIX .elf)

add_executable(${PROJECT_NAME}.elf
  ${APP_SOURCES}
  ${CMSIS_SOURCES}
  ${HAL_SOURCES}
  ${LWIP_SOURCES}
)

set_target_properties(${PROJECT_NAME}.elf PROPERTIES
  OUTPUT_NAME ${PROJECT_NAME}
  # Re-link automatically whenever the linker script changes.
  LINK_DEPENDS ${LINKER_SCRIPT}
)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
  COMMAND ${CMAKE_OBJCOPY} -O ihex  $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${PROJECT_NAME}.bin
  COMMENT "Generating ${PROJECT_NAME}.hex / ${PROJECT_NAME}.bin"
)
