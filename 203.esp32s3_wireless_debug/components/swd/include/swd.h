/**
 * @file swd.h
 * @brief SWD bit-bang engine for ESP32-S3 (GPIO register level timing).
 *
 * Layering:  cmsis_dap -> debug_engine -> swd  (never skip layers)
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

/* SWD ACK / response codes (same values as CMSIS-DAP DAP_TRANSFER_*) */
#define SWD_ACK_OK           0x01u
#define SWD_ACK_WAIT         0x02u
#define SWD_ACK_FAULT        0x04u
#define SWD_ACK_NO_RESPONSE  0x07u  /* protocol error / target absent   */
#define SWD_ACK_PARITY_ERROR 0x08u  /* local: data parity mismatch      */

/* Debug Port register addresses (A[3:2] of the request byte) */
#define SWD_DP_ADDR_IDLE     0x00u
#define SWD_DP_ADDR_IDCODE   0x00u  /* read only  */
#define SWD_DP_ADDR_ABORT    0x00u  /* write only */
#define SWD_DP_ADDR_CTRLSTAT 0x04u
#define SWD_DP_ADDR_SELECT   0x08u  /* write only */
#define SWD_DP_ADDR_RESEND   0x08u  /* read only  */
#define SWD_DP_ADDR_RDBUFF   0x0Cu  /* read only  */

/* DP CTRL/STAT bits */
#define DP_CSYSPWRUPREQ      (1uL << 30)
#define DP_CSYSPWRUPACK      (1uL << 29)
#define DP_CDBGPWRUPREQ      (1uL << 28)
#define DP_CDBGPWRUPACK      (1uL << 27)
#define DP_CDBGRSTACK        (1uL << 26)
#define DP_STICKYERR         (1uL << 5)

/* Request byte builder: addr8 is the DP/AP byte address (0x00/0x04/0x08/0x0C).
 * A[3:2] lives in the address bits 3,2 and maps directly to request bits 3,2
 * (A3=bit3, A2=bit2), so the field is (addr8 & 0x0Cu). The previous form
 * ((addr8 >> 2) & 0x0Cu) always collapsed to 0, silently forcing every DP
 * access to the IDCODE/ABORT register (0x00) and breaking CTRLSTAT/SELECT/
 * RDBUFF - which is why only IDCODE reads ever worked over SWD. */
#define SWD_REQ(addr8, ap, read) \
    (uint8_t)((ap) | ((read) ? 0x02u : 0x00u) | ((addr8) & 0x0Cu))

/**
 * @brief Configure SWD GPIOs (safe idle state). Call once at boot.
 */
esp_err_t swd_init(void);

/**
 * @brief Full SWD protocol connect: idle pins -> line reset -> JTAG-to-SWD
 *        sequence -> line reset -> verify DPIDR -> clear errors.
 *        Does NOT power up the DP (that is the host's job via DAP_Transfer).
 */
esp_err_t swd_connect(void);

/**
 * @brief Release the bus: SWCLK low, SWDIO high (output), nRESET released.
 */
void swd_set_idle(void);

/**
 * @brief Set SWD clock. Value is clamped to CONFIG_DEBUG_SWD_MAX_CLOCK_HZ.
 *        The actual generated clock is always <= requested (safe direction).
 */
esp_err_t swd_set_clock(uint32_t hz);
uint32_t swd_get_clock(void);

/**
 * @brief >50 SWCLK cycles with SWDIO high, then return to idle.
 */
esp_err_t swd_line_reset(void);

/**
 * @brief 16-bit JTAG-to-SWD magic sequence 0xE79E (LSB first).
 */
esp_err_t swd_jtag_to_swd(void);

/**
 * @brief 16-bit SWD-to-JTAG magic sequence 0xE73E (LSB first).
 *        SWDIO/TMS and SWCLK/TCK are the same pins, so this can be emitted
 *        by the SWD engine to return a DP left in SWD mode back to JTAG.
 *        Callers wrap it with swd_line_reset() (>=50 idle cycles) on both
 *        sides.
 */
esp_err_t swd_swd_to_jtag(void);

/**
 * @brief Raw SWD transfer. Mirrors ARM's SWD_Transfer() semantics.
 *
 * @param request DAP_TRANSFER-style request byte (bit0 APnDP, bit1 RnW,
 *                bit2 A2, bit3 A3)
 * @param data    read: value out; write: value in (NULL allowed on reads)
 * @return SWD_ACK_OK / SWD_ACK_WAIT / SWD_ACK_FAULT / SWD_ACK_NO_RESPONSE /
 *         SWD_ACK_PARITY_ERROR
 */
uint8_t swd_transfer(uint8_t request, uint32_t *data);

/* Convenience accessors (single transfers, no retry logic) */
esp_err_t swd_read_dp(uint8_t addr, uint32_t *data);
esp_err_t swd_write_dp(uint8_t addr, uint32_t data);
esp_err_t swd_read_ap(uint8_t addr, uint32_t *data);   /* posts read + RDBUFF */
esp_err_t swd_write_ap(uint8_t addr, uint32_t data);   /* write + flush       */

/**
 * @brief Send arbitrary bit sequence LSB-first on SWDIO (DAP_SWJ_Sequence).
 * @param count_bits number of bits (data buffer ceil(count/8) bytes)
 */
esp_err_t swd_swj_sequence(uint32_t count_bits, const uint8_t *data);

/**
 * @brief Raw SWDIO capture/drive sequence (DAP_SWD_Sequence payload).
 *        Callers handle output-enable themselves via swd_swdio_output().
 */
void swd_sequence_out(uint32_t nbits, const uint8_t *data);
void swd_sequence_in(uint32_t nbits, uint8_t *data);

void swd_swdio_output(bool enable);

/* nRESET control: open-drain, asserted = driven low */
esp_err_t swd_reset_assert(bool asserted);
esp_err_t swd_reset_pulse(void);   /* assert ~100ms then release */
bool swd_nreset_read(void);

/* Raw pin control for DAP_SWJ_Pins */
void swd_pin_swclk(bool high);
void swd_pin_swdio(bool high);
bool swd_pin_swclk_in(void);
bool swd_pin_swdio_in(void);

/* Transfer behaviour configuration (DAP_SWD_Configure / DAP_TransferConfigure) */
void swd_set_turnaround(uint8_t cycles);   /* 1..4, default 1 */
void swd_set_data_phase(bool always);      /* default false   */
void swd_set_idle_cycles(uint8_t n);       /* after OK transfer */

#ifdef __cplusplus
}
#endif
