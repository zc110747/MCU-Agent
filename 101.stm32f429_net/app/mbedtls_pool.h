/**
  ******************************************************************************
  * @file    mbedtls_pool.h
  * @brief   mbedTLS static heap pool (external SDRAM) declaration.
  ******************************************************************************
  */
#ifndef MBEDTLS_POOL_H
#define MBEDTLS_POOL_H

/* 256 KB: allows up to HTTPS_MAX_CONNS (4) concurrent ECDHE-ECDSA handshakes
 * plus their TLS session state, with headroom for mbedTLS buffer-alloc
 * fragmentation. SDRAM is 32 MB so this is cheap. */
#define MBEDTLS_POOL_SIZE (256u * 1024u)

/* Defined in mbedtls_pool.c (section .mbedtls_pool, NOLOAD, in SDRAM). */
extern uint8_t mbedtls_heap[MBEDTLS_POOL_SIZE];

#endif /* MBEDTLS_POOL_H */
