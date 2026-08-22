cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/arm-none-eabi-gcc.cmake CACHE FILEPATH "" FORCE)
project(stm32f429_net C ASM)

# ------------------------------------------------------------------
# MCU / toolchain flags
# ------------------------------------------------------------------
# soft-float ABI, but allow FPU instructions: the FreeRTOS ARM_CM4F port
# contains unconditional vstmdbeq/vldmiaeq {s16-s31} which the assembler
# rejects with plain -mfloat-abi=soft.  The C code stays float-free and the
# port only saves FPU regs when a task actually uses them (runtime check).
set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -mfpu=fpv4-sp-d16")

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
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MCU_FLAGS} -T${LINKER_SCRIPT} -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map -Wl,--gc-sections -Wl,--print-memory-usage -specs=nano.specs -u _printf_float -lc -lm -lgcc")

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
  third_party/FreeRTOS-Kernel/include
  third_party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F
  third_party/mbedtls/include
)

add_compile_definitions(STM32F429xx USE_HAL_DRIVER)
# mbedTLS: point MBEDTLS_CONFIG_FILE at our whitelist config (app/mbedtls_config.h).
# Angle-bracket form is required: quote form would resolve to the stock config
# in include/mbedtls/ (quote-include searches the includer's directory first).
add_compile_definitions(MBEDTLS_CONFIG_FILE=<mbedtls_config.h>)

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
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sdram.c
  Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_fmc.c
)

# FreeRTOS V11 kernel (native API): core + ARM_CM4F port + heap_4.
set(FREERTOS_SOURCES
  third_party/FreeRTOS-Kernel/tasks.c
  third_party/FreeRTOS-Kernel/queue.c
  third_party/FreeRTOS-Kernel/list.c
  third_party/FreeRTOS-Kernel/timers.c
  third_party/FreeRTOS-Kernel/event_groups.c
  third_party/FreeRTOS-Kernel/stream_buffer.c
  third_party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c
  third_party/FreeRTOS-Kernel/portable/MemMang/heap_4.c
)

# LwIP core for an RTOS (NO_SYS=0), IPv4, static-IP build. Exclude the
# PPP/IPv6/templates and the parts we don't use (sockets/netdb); keep the
# netconn API (tcpip thread + api_msg) used by the HTTP server.
file(GLOB_RECURSE LWIP_SOURCES ${CMAKE_SOURCE_DIR}/third_party/LwIP/src/*.c)
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/apps/.*")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/ppp/.*")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/ipv6/.*")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/netif/lowpan6.*")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/netif/bridgeif.*")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/netif/slipif\\.c")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/netif/zepif\\.c")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/core/sys\\.c")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/core/altcp.*\\.c")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/api/sockets\\.c")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/api/netdb\\.c")
list(FILTER LWIP_SOURCES EXCLUDE REGEX ".*/api/if_api\\.c")

# mbedTLS: exactly the modules enabled by app/mbedtls_config.h (TLS 1.2 server,
# ECDHE-ECDSA P-256, AES-128-GCM, SHA-256, X509/PK parse, buffer allocator).
set(MBEDTLS_SOURCES
  third_party/mbedtls/library/aes.c
  third_party/mbedtls/library/asn1parse.c
  third_party/mbedtls/library/asn1write.c
  third_party/mbedtls/library/base64.c
  third_party/mbedtls/library/bignum.c
  third_party/mbedtls/library/bignum_core.c
  third_party/mbedtls/library/bignum_mod.c
  third_party/mbedtls/library/bignum_mod_raw.c
  third_party/mbedtls/library/block_cipher.c
  third_party/mbedtls/library/cipher.c
  third_party/mbedtls/library/cipher_wrap.c
  third_party/mbedtls/library/constant_time.c
  third_party/mbedtls/library/ecdh.c
  third_party/mbedtls/library/ecdsa.c
  third_party/mbedtls/library/ecp.c
  third_party/mbedtls/library/ecp_curves.c
  third_party/mbedtls/library/error.c
  third_party/mbedtls/library/gcm.c
  third_party/mbedtls/library/md.c
  third_party/mbedtls/library/memory_buffer_alloc.c
  third_party/mbedtls/library/oid.c
  third_party/mbedtls/library/pem.c
  third_party/mbedtls/library/pk.c
  third_party/mbedtls/library/pk_ecc.c
  third_party/mbedtls/library/pk_wrap.c
  third_party/mbedtls/library/pkparse.c
  third_party/mbedtls/library/platform.c
  third_party/mbedtls/library/platform_util.c
  third_party/mbedtls/library/sha1.c
  third_party/mbedtls/library/sha256.c
  third_party/mbedtls/library/ssl_ciphersuites.c
  third_party/mbedtls/library/ssl_msg.c
  third_party/mbedtls/library/ssl_tls.c
  third_party/mbedtls/library/ssl_tls12_server.c
  third_party/mbedtls/library/x509.c
  third_party/mbedtls/library/x509_crt.c
)

# ------------------------------------------------------------------
# Executable
# ------------------------------------------------------------------
set(CMAKE_EXECUTABLE_SUFFIX .elf)

add_executable(${PROJECT_NAME}.elf
  ${APP_SOURCES}
  ${CMSIS_SOURCES}
  ${HAL_SOURCES}
  ${LWIP_SOURCES}
  ${FREERTOS_SOURCES}
  ${MBEDTLS_SOURCES}
)

# LwIP 2.1 quirk: api_lib.c computes polls_left with a large constant that
# overflows u8_t (third-party code, warning only). Silence for that file.
set_source_files_properties(
  third_party/LwIP/src/api/api_lib.c
  PROPERTIES COMPILE_OPTIONS "-Wno-overflow"
)

# FreeRTOS ARM_CM4F port has unconditional vstmdbeq/vldmiaeq {s16-s31} in
# its naked PendSV handler.  With -mfloat-abi=soft the assembler disables
# the FPU instruction set entirely, so compile just this file with softfp
# (same call ABI, FPU instructions allowed).  All other files stay soft.
set_source_files_properties(
  third_party/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c
  PROPERTIES COMPILE_OPTIONS "-mfloat-abi=softfp;-mfpu=fpv4-sp-d16"
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
