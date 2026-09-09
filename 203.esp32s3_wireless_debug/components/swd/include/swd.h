/**
 * @file swd.h
 * @brief SWD bit-bang engine for ESP32-S3 (GPIO register level timing).
 *
 * Layering:  cmsis_dap -> debug_engine -> swd  (never skip layers)
 *
 * Architecture follows huming2207/swd-esp (DAPLink swd_host + CMSIS-DAP
 * SW_DP) adapted to a single module:
 *   - bit level:    GPIO register direct writes, IRAM hot path, no logging
 *   - host level:   DAPLink-style swd_read_ap/swd_write_ap (DP_SELECT before
 *                   every AP access, dummy-read-first on AP reads, WAIT retry)
 *   - target level: MEM-AP memory access, Cortex-M debug (DHCSR halt/run)
 *   - connect:      connect-under-reset (nRESET held LOW until the core is
 *                   confirmed halted, then released and re-checked)
 *
 * All transfer functions return an ACK-style code compatible with the
 * CMSIS-DAP specification (SWD_ACK_*), so the DAP protocol layer can embed
 * them directly into DAP_Transfer responses.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* SWD ACK / response codes (same values as CMSIS-DAP DAP_TRANSFER_*)  */
/* ------------------------------------------------------------------ */
#define SWD_ACK_OK           0x01u
#define SWD_ACK_WAIT         0x02u
#define SWD_ACK_FAULT        0x04u
#define SWD_ACK_NO_RESPONSE  0x07u  /* protocol error / target absent   */
#define SWD_ACK_PARITY_ERROR 0x08u  /* local: data parity mismatch      */

/* ------------------------------------------------------------------ */
/* Debug Port (SW-DP) register addresses (A[3:2] of the request byte)  */
/* ------------------------------------------------------------------ */
#define SWD_DP_ADDR_IDLE     0x00u
#define SWD_DP_ADDR_IDCODE   0x00u  /* read only  */
#define SWD_DP_ADDR_ABORT    0x00u  /* write only */
#define SWD_DP_ADDR_CTRLSTAT 0x04u
#define SWD_DP_ADDR_SELECT   0x08u  /* write only */
#define SWD_DP_ADDR_RESEND   0x08u  /* read only  */
#define SWD_DP_ADDR_RDBUFF   0x0Cu  /* read only  */

/* DP ABORT register bits (debug_cm.h) */
#define DP_DAPABORT          0x00000001u
#define DP_STKCMPCLR         0x00000002u
#define DP_STKERRCLR         0x00000004u
#define DP_WDERRCLR          0x00000008u
#define DP_ORUNERRCLR        0x00000010u

/* DP CTRL/STAT bits (debug_cm.h) */
#define DP_CSYSPWRUPREQ      (1uL << 30)
#define DP_CSYSPWRUPACK      (1uL << 29)
#define DP_CDBGPWRUPREQ      (1uL << 28)
#define DP_CDBGPWRUPACK      (1uL << 27)
#define DP_CDBGRSTACK        (1uL << 26)
#define DP_TRNNORMAL         0x00000000u  /* transfer mode: normal      */
#define DP_MASKLANE          0x00000F00u  /* mask lanes (DAPLink value) */
#define DP_STICKYERR         (1uL << 5)
#define DP_WDATAERR          (1uL << 7)

/* Request byte builder: addr8 is the DP/AP byte address (0x00/0x04/0x08/0x0C).
 * A[3:2] lives in the address bits 3,2 and maps directly to request bits 3,2. */
#define SWD_REQ(addr8, ap, read) \
    (uint8_t)((ap) | ((read) ? 0x02u : 0x00u) | ((addr8) & 0x0Cu))

/* ------------------------------------------------------------------ */
/* MEM-AP registers (debug_cm.h)                                       */
/* ------------------------------------------------------------------ */
#define AP_CSW               0x00u  /* Control and Status Word  */
#define AP_TAR               0x04u  /* Transfer Address         */
#define AP_DRW               0x0Cu  /* Data Read/Write          */
#define AP_IDR               0xFCu  /* Identification Register  */

/* AP CSW fields */
#define CSW_SIZE8            0x00000000u
#define CSW_SIZE16           0x00000001u
#define CSW_SIZE32           0x00000002u
#define CSW_NADDRINC         0x00000000u
#define CSW_SADDRINC         0x00000010u
#define CSW_DBGSTAT          0x00000040u
#define CSW_HPROT            0x02000000u
#define CSW_MSTRDBG          0x20000000u
#define CSW_RESERVED         0x01000000u
/* DAPLink base CSW: reserved | master=debug | HPROT | debug-stat | single-inc */
#define CSW_VALUE (CSW_RESERVED | CSW_MSTRDBG | CSW_HPROT | CSW_DBGSTAT | CSW_SADDRINC)

/* ------------------------------------------------------------------ */
/* Cortex-M core debug registers (debug_cm.h)                          */
/* ------------------------------------------------------------------ */
#define CM_DHCSR             0xE000EDF0u  /* Debug Halting Ctrl & Status    */
#define CM_DCRSR             0xE000EDF4u  /* Debug Core Register Selector   */
#define CM_DCRDR             0xE000EDF8u  /* Debug Core Register Data       */
#define CM_DEMCR             0xE000EDFCu  /* Debug Exception & Monitor Ctrl */

#define CM_DBGKEY            0xA05F0000u
#define CM_C_DEBUGEN         0x00000001u
#define CM_C_HALT            0x00000002u
#define CM_C_STEP            0x00000004u
#define CM_C_MASKINTS        0x00000008u
#define CM_S_REGRDY          0x00010000u
#define CM_S_HALT            0x00020000u
#define CM_S_SLEEP           0x00040000u
#define CM_S_LOCKUP          0x00080000u
#define CM_S_RESET_ST        0x02000000u

/* ------------------------------------------------------------------ */
/* Connect stage error classification                                  */
/* ------------------------------------------------------------------ */
typedef enum {
    SWD_STAGE_OK = 0,
    SWD_ERR_LINE_RESET,     /* SWD line reset failed                */
    SWD_ERR_JTAG_TO_SWD,    /* JTAG-to-SWD switch failed            */
    SWD_ERR_DPIDR,          /* DPIDR read failed (first checkpoint) */
    SWD_ERR_ABORT,          /* DP ABORT / sticky clear failed       */
    SWD_ERR_POWERUP,        /* DP CDBGPWRUPREQ/CSYSPWRUPREQ failed  */
    SWD_ERR_AP,             /* MEM-AP (APSEL0) init / IDR failed    */
    SWD_ERR_MEM,            /* MEM-AP data access failed            */
    SWD_ERR_DHCSR,          /* DHCSR read/write failed              */
    SWD_ERR_HALT,           /* S_HALT not confirmed                 */
} swd_error_stage_t;

const char *swd_stage_name(swd_error_stage_t stage);
swd_error_stage_t swd_last_error_stage(void);

/* ------------------------------------------------------------------ */
/* GPIO init / bus state                                               */
/* ------------------------------------------------------------------ */
esp_err_t swd_init(void);
void swd_set_idle(void);

/**
 * @brief Set SWD clock. Value is clamped to CONFIG_DEBUG_SWD_MAX_CLOCK_HZ.
 *        The half-bit delay is calibrated at runtime by measuring the real
 *        GPIO toggle cost (cycle counter) - no magic numbers. Iterations are
 *        rounded up, so the ACTUAL generated clock is never faster than the
 *        requested one; if the requested rate is not achievable bit-banged,
 *        the actual clock is lower.
 */
esp_err_t swd_set_clock(uint32_t hz);
uint32_t swd_get_clock(void);         /* requested clock   */
uint32_t swd_get_actual_clock(void);  /* estimated actual  */

/* Transfer behaviour configuration (DAP_SWD_Configure / DAP_TransferConfigure) */
void swd_set_turnaround(uint8_t cycles);   /* 1..4, default 1 */
void swd_set_data_phase(bool always);      /* default false   */
void swd_set_idle_cycles(uint8_t n);       /* after OK transfer */

/* ------------------------------------------------------------------ */
/* Sequences                                                           */
/* ------------------------------------------------------------------ */
esp_err_t swd_line_reset(void);            /* >=50 SWCLK cycles, SWDIO high */
esp_err_t swd_jtag_to_swd(void);           /* 0xE79E LSB-first              */
esp_err_t swd_swd_to_jtag(void);           /* 0xE73E LSB-first              */

esp_err_t swd_swj_sequence(uint32_t count_bits, const uint8_t *data);
void swd_sequence_out(uint32_t nbits, const uint8_t *data);
void swd_sequence_in(uint32_t nbits, uint8_t *data);
void swd_swdio_output(bool enable);

/* ------------------------------------------------------------------ */
/* Raw transfer (SW_DP.c semantics)                                    */
/* ------------------------------------------------------------------ */
uint8_t swd_transfer(uint8_t request, uint32_t *data);
uint8_t swd_transfer_retry(uint8_t request, uint32_t *data);  /* WAIT retry */

/* ------------------------------------------------------------------ */
/* DP / AP register access (DAPLink swd_host semantics)                */
/* ------------------------------------------------------------------ */
esp_err_t swd_read_dp(uint8_t addr, uint32_t *data);
esp_err_t swd_write_dp(uint8_t addr, uint32_t data);
/* AP accessors write DP_SELECT(AP0/bank0) before every access; AP reads
 * perform the required dummy read (posted read) first. */
esp_err_t swd_read_ap(uint8_t addr, uint32_t *data);
esp_err_t swd_write_ap(uint8_t addr, uint32_t data);

esp_err_t swd_clear_errors(void);          /* DP ABORT: clear sticky errors */
esp_err_t swd_read_idcode(uint32_t *id);   /* 8 idle bits + DPIDR read      */

/* ------------------------------------------------------------------ */
/* MEM-AP memory access (32-bit)                                       */
/* ------------------------------------------------------------------ */
esp_err_t swd_mem_read32(uint32_t addr, uint32_t *data);
esp_err_t swd_mem_write32(uint32_t addr, uint32_t data);

/* ------------------------------------------------------------------ */
/* Cortex-M debug control (via MEM-AP -> DHCSR)                        */
/* ------------------------------------------------------------------ */
esp_err_t swd_halt(void);      /* DBGKEY | C_DEBUGEN | C_HALT, confirm S_HALT */
esp_err_t swd_run(void);       /* DBGKEY | C_DEBUGEN                          */
esp_err_t swd_is_halted(bool *halted);     /* read DHCSR, test S_HALT         */
esp_err_t swd_wait_until_halted(uint32_t timeout_ms);

/* ------------------------------------------------------------------ */
/* Full protocol connect                                               */
/* ------------------------------------------------------------------ */
/**
 * @brief Connect to the target with the DAPLink "connect under reset" flow.
 *
 * With nRESET wired (recommended; required for targets whose user program
 * remaps the SWD pins):
 *   assert nRESET and HOLD IT LOW for the whole bring-up:
 *   line reset -> JTAG-to-SWD -> line reset -> DPIDR -> ABORT clear ->
 *   SELECT 0 -> DP power-up -> AP0 CSW init -> MEM-AP DHCSR
 *   C_DEBUGEN=1 + C_HALT=1 -> confirm S_HALT -> release nRESET ->
 *   re-confirm the core is still halted.
 *
 * Without nRESET: normal connect (JTAG2SWD + DP init) then halt attempt.
 *
 * @return ESP_OK only when the link is up AND the core is confirmed halted.
 */
esp_err_t swd_connect(void);

/* ------------------------------------------------------------------ */
/* nRESET control (open-drain, asserted = driven low)                  */
/* ------------------------------------------------------------------ */
esp_err_t swd_reset_assert(bool asserted);
esp_err_t swd_reset_pulse(void);   /* assert ~100ms then release */
bool swd_nreset_read(void);

/* Raw pin control for DAP_SWJ_Pins */
void swd_pin_swclk(bool high);
void swd_pin_swdio(bool high);
bool swd_pin_swclk_in(void);
bool swd_pin_swdio_in(void);

#ifdef __cplusplus
}
#endif
