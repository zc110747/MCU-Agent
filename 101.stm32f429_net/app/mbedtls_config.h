/*
 * mbedtls_config.h - minimal TLS 1.2 *server* config for STM32F429 (FreeRTOS).
 *
 * Whitelist only: anything not defined here is OFF.  Goal is the smallest
 * footprint that can serve HTTPS with an ECDSA (P-256) certificate:
 *
 *     TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
 *
 * Design choices for a constrained Cortex-M4:
 *   - TLS 1.2 only (no 1.0/1.1/1.3, no DTLS, no renegotiation).
 *   - ECDHE-ECDSA key exchange; certificate is EC P-256 / ECDSA.
 *   - AES-128-GCM + SHA-256 for the record / PRF.
 *   - No filesystem, no sockets, no threads, no entropy/DRBG module:
 *     we feed mbedTLS a custom software RNG (see https_server.c).
 *   - mbedTLS heap comes from a static pool in EXTERNAL SDRAM via
 *     MBEDTLS_MEMORY_BUFFER_ALLOC_C (app/mbedtls_pool.c, section
 *     .mbedtls_pool @0xC0000000+).
 *   - Per-connection buffers shrunk (4 KiB) since we only serve a tiny page.
 *
 * Pointed at by -DMBEDTLS_CONFIG_FILE=<mbedtls_config.h> in CMakeLists.txt
 * (angle brackets required: quote form would resolve to the stock config
 * in include/mbedtls/).
 */
#ifndef MBEDTLS_CONFIG_H_
#define MBEDTLS_CONFIG_H_

/* ---- platform / error reporting ---- */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY      /* route calloc/free to the static pool */
#define MBEDTLS_ERROR_C
#define MBEDTLS_MEMORY_BUFFER_ALLOC_C
#define MBEDTLS_MEMORY_DEBUG         /* heap cur/max stats for bring-up sizing */

/* ---- bignum / EC ---- */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
/* NOTE: X25519 tested slower than P-256 here -- mbedTLS 3.6 has no Everest
 * fast path, so X25519 falls back to the generic Montgomery ladder which is
 * slower than the NIST-optimized P-256.  Keep P-256 only. */
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
/* P-256 speed-ups: NIST fast reduction + fixed-point precomputed table.
 * Without these, an ECDHE handshake on a 180 MHz core takes ~3.6-4.9 s
 * (general bignum reduction); with them it drops to tens of ms.  Window 6
 * needs a few KB ROM for the precomputed table. */
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_WINDOW_SIZE        6
#define MBEDTLS_ECP_FIXED_POINT_OPTIM  1
/* Cortex-M4 DSP extension: bignum multiply via umull/umlal inline asm
 * (bn_mul.h ARMv6+ fast path, __ARM_FEATURE_DSP=1 with -mcpu=cortex-m4). */
#define MBEDTLS_HAVE_ASM

/* ---- symmetric / hashes ---- */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_GCM
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA1_C            /* kept (small) to satisfy X509/compat refs */

/* ---- ASN.1 / PEM / base64 / OID ---- */
#define MBEDTLS_BASE64_C
#define MBEDTLS_OID_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C

/* ---- X.509 / public key (parse only; cert is host-generated) ---- */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C

/* ---- TLS ---- */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED

/* Don't retain the peer cert on the server (we don't verify clients). */
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE 0

/* No renegotiation (saves code, removes a state we'd have to drive). */
#define MBEDTLS_SSL_RENEGOTIATION 0

/* Shrink per-connection record buffers: we only serve a ~1.7 KiB page. */
#define MBEDTLS_SSL_IN_CONTENT_LEN  4096
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096

/* Implicitly OFF (listed for clarity / future readers):
 *   MBEDTLS_PSA_CRYPTO_C, MBEDTLS_ENTROPY_C, MBEDTLS_CTR_DRBG_C,
 *   MBEDTLS_HMAC_DRBG_C, MBEDTLS_RSA_C, MBEDTLS_DHM_C, MBEDTLS_NET_C,
 *   MBEDTLS_FS_IO, MBEDTLS_TIMING_C, MBEDTLS_THREADING_C, MBEDTLS_DEBUG_C,
 *   MBEDTLS_SSL_CLI_C, MBEDTLS_SSL_TICKET_C, MBEDTLS_X509_CRT_WRITE_C,
 *   MBEDTLS_PK_WRITE_C, MBEDTLS_PKCS5_C, MBEDTLS_PKCS12_C, MBEDTLS_MD5_C,
 *   MBEDTLS_SHA512_C, MBEDTLS_DES_C, MBEDTLS_3DES, MBEDTLS_ARC4,
 *   MBEDTLS_CAMELLIA_C, MBEDTLS_ARIA_C, MBEDTLS_CHACHAPOLY_C, MBEDTLS_CCM_C,
 *   MBEDTLS_NIST_KW_C, MBEDTLS_RIPEMD160_C, MBEDTLS_ECP_FIXED_POINT_OPTIM,
 *   MBEDTLS_SSL_PROTO_TLS1_0, MBEDTLS_SSL_PROTO_TLS1_1, MBEDTLS_SSL_PROTO_TLS1_3,
 *   MBEDTLS_SSL_ALPN, MBEDTLS_SSL_SESSION_TICKETS
 */

/* mbedTLS 3.6 known false positive: common.h 128-bit xor reads a
 * 16-byte object as uint64_t[2] with index [2]; GCC -O3 -Warray-bounds
 * flags it.  This pragma only affects files that include mbedTLS headers. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

#endif /* MBEDTLS_CONFIG_H_ */
