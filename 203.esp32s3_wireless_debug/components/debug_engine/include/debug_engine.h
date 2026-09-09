/**
 * @file debug_engine.h
 * @brief Cortex-M debug operations on top of the SWD engine.
 *
 * The Debug Engine owns the SWD engine. Every entry point (USB CMSIS-DAP,
 * future Wi-Fi GDB, UART bridge) must hold the debug mutex while driving
 * debug operations, via debug_engine_lock()/debug_engine_unlock().
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Core debug register block */
#define DHCSR_ADDR  0xE000EDF0u
#define DCRSR_ADDR  0xE000EDF4u
#define DCRDR_ADDR  0xE000EDF8u
#define DEMCR_ADDR  0xE000EDFCu
#define AIRCR_ADDR  0xE000ED0Cu
#define DFSR_ADDR   0xE000ED30u
#define CPUID_ADDR  0xE000ED00u

/* DHCSR bits */
#define DHCSR_DBGKEY        0xA05F0000u
#define DHCSR_C_DEBUGEN     (1uL << 0)
#define DHCSR_C_HALT        (1uL << 1)
#define DHCSR_C_STEP        (1uL << 3)
#define DHCSR_C_MASKINTS    (1uL << 4)
#define DHCSR_S_REGRDY      (1uL << 16)
#define DHCSR_S_HALT        (1uL << 17)
#define DHCSR_S_RESET_ST    (1uL << 24)

/* AIRCR */
#define AIRCR_VECTKEY       (0x5FAuL << 16)
#define AIRCR_SYSRESETREQ   (1uL << 2)

/* DEMCR */
#define DEMCR_TRCENA        (1uL << 24)
#define DEMCR_VC_CORERESET  (1uL << 0)

/* MEM-AP register offsets (A[3:2] values used with AP accesses) */
#define MEM_AP_CSW  0x00u
#define MEM_AP_TAR  0x04u
#define MEM_AP_DRW  0x0Cu
#define MEM_AP_IDR  0xFCu

/* CSW fields (ADIv5). Size field [2:0]: 000=8-bit, 001=16-bit, 010=32-bit.
 * (Was previously mis-encoded: 8-bit held 0x2, 32-bit held 0x0, which made
 * every MEM-AP word access a byte access and broke DHCSR writes.)
 * AddrInc field [5:4]: single-word accesses use no-increment (0x0),
 * block accesses use single-increment (0x10). */
#define CSW_BASE        (0x01000000u | 0x20000000u | 0x02000000u | 0x00000040u) \
                        /* reserved | MasterType=debug | HPROT | DbgStatus */
#define CSW_ADDRINC_SINGLE  0x00000000u
#define CSW_ADDRINC_AUTO    0x00000010u
#define CSW_SIZE_8          0x00000000u
#define CSW_SIZE_16         0x00000001u
#define CSW_SIZE_32         0x00000002u

/* Unified error codes */
typedef enum {
    DEBUG_OK = 0,
    DEBUG_ERROR,
    DEBUG_TIMEOUT,
    DEBUG_WAIT,
    DEBUG_FAULT,
    DEBUG_NO_TARGET,
    DEBUG_PARITY_ERROR,
    DEBUG_PROTOCOL_ERROR,
    DEBUG_NOT_CONNECTED,
    DEBUG_INVALID_ADDRESS,
} debug_err_t;

/**
 * @brief Initialise the debug engine (mutex + SWD pins). Call once at boot.
 */
esp_err_t debug_init(void);

/* Debug mutex: acquired by every transport (USB task today, Wi-Fi tomorrow)
 * around whole DAP command processing so SWD is never driven concurrently. */
esp_err_t debug_engine_lock(uint32_t timeout_ms);
void debug_engine_unlock(void);

/**
 * @brief Full connect: SWD protocol connect + power up DP + select AP 0.
 */
esp_err_t debug_connect(void);
esp_err_t debug_disconnect(void);

esp_err_t debug_reset(bool hard_reset);   /* SYSRESETREQ or nRESET pulse */
esp_err_t debug_halt(void);
esp_err_t debug_run(void);
esp_err_t debug_step(void);
esp_err_t debug_is_halted(bool *halted);

/**
 * @brief Core register access. @param reg uses DCRSR REGSEL numbering:
 *        0-15 R0-R15, 16 xPSR, 17 MSP, 18 PSP, 19 PRIMASK, 20 CONTROL,
 *        21 BASEPRI, 22 FAULTMASK. Target must be halted.
 */
esp_err_t debug_read_register(uint32_t reg, uint32_t *value);
esp_err_t debug_write_register(uint32_t reg, uint32_t value);

/* Memory access via MEM-AP (CSW/TAR/DRW, 1 KB autoincrement boundary safe) */
esp_err_t debug_read_memory(uint32_t address, void *buffer, size_t size);
esp_err_t debug_write_memory(uint32_t address, const void *buffer, size_t size);
esp_err_t debug_read_word(uint32_t address, uint32_t *value);
esp_err_t debug_write_word(uint32_t address, uint32_t value);

#ifdef __cplusplus
}
#endif
