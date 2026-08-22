/**
  ******************************************************************************
  * @file    mbedtls_pool.c
  * @brief   mbedTLS static heap pool placed in external SDRAM.
  *
  *          Used via MBEDTLS_MEMORY_BUFFER_ALLOC_C + MBEDTLS_PLATFORM_MEMORY:
  *          every mbedtls_calloc/free routes here.  The .mbedtls_pool section
  *          is NOLOAD in the linker script (startup must not zero SDRAM before
  *          FMC init); the pool is initialised in https_server_init().
  ******************************************************************************
  */
#include <stdint.h>
#include "mbedtls_pool.h"

uint8_t mbedtls_heap[MBEDTLS_POOL_SIZE]
  __attribute__((section(".mbedtls_pool"), aligned(8)));
