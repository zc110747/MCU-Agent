/**
 * @file jtag.h
 * @brief JTAG bit-bang engine for ESP32-S3 (GPIO register level timing).
 *
 * Pure JTAG (no SWD<->JTAG switching). TCK/TMS are shared with the SWD
 * engine's SWCLK/SWDIO (same physical probe pins), TDI/TDO/nTRST are extra.
 *
 * The core I/O functions (jtag_ir / jtag_transfer / jtag_read_idcode /
 * jtag_write_abort / jtag_sequence) are a verbatim port of ARM's official
 * JTAG_DP.c (CMSIS-DAP v2.0.0) with our register-level GPIO macros, so the
 * wire protocol is byte-identical to the reference firmware OpenOCD/Keil know.
 *
 * Layering:  cmsis_dap -> debug_engine -> jtag  (never skip layers)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* JTAG IR values (ARM ADIv5) */
#define JTAG_IR_ABORT   0x08U
#define JTAG_IR_DPACC   0x0AU
#define JTAG_IR_APACC   0x0BU
#define JTAG_IR_IDCODE  0x0EU
#define JTAG_IR_BYPASS  0x0FU

/* Transfer request / response bits (same encoding as CMSIS-DAP DAP_TRANSFER).
 * Defined locally so jtag.c has no dependency on the CMSIS-DAP protocol layer. */
#define JTAG_TRANSFER_APnDP  (1u << 0)
#define JTAG_TRANSFER_RnW    (1u << 1)
#define JTAG_TRANSFER_A2     (1u << 2)
#define JTAG_TRANSFER_A3     (1u << 3)
#define JTAG_TRANSFER_OK     0x01u
#define JTAG_TRANSFER_WAIT   0x02u
#define JTAG_TRANSFER_FAULT  0x04u
#define JTAG_TRANSFER_ERROR  0x08u
/* Same bit position as DAP_TRANSFER_MISMATCH (0x10) in the DAP response */
#define JTAG_TRANSFER_MISMATCH 0x10u

/* Max TAPs supported in a single JTAG chain (matches ARM DAP_JTAG_DEV_CNT). */
#define JTAG_TAP_MAX 8

/* JTAG request byte: bit0 APnDP, bit1 RnW, bit2 A2, bit3 A3.
 * Identical encoding to the SWD DAP_TRANSFER request byte the host sends. */
#define JTAG_REQ_RDBUFF_READ 0x0Eu   /* read RDBUFF (flush posted read) */

/**
 * @brief Configure JTAG GPIOs (safe idle state). Call once at boot.
 */
esp_err_t jtag_init(void);

/**
 * @brief Release the bus: TCK low, TMS high, TDI high, nTRST released.
 */
void jtag_set_idle(void);

/**
 * @brief Set JTAG clock. Clamped to CONFIG_DEBUG_JTAG_MAX_CLOCK_HZ; the
 *        generated clock is always <= requested (safe direction).
 */
esp_err_t jtag_set_clock(uint32_t hz);
uint32_t jtag_get_clock(void);

/**
 * @brief Idle TCK cycles inserted after each transfer (DAP_TransferConfigure).
 */
void jtag_set_idle_cycles(uint8_t n);

/**
 * @brief Full JTAG protocol connect: nRESET pulse (via shared SWD pin) ->
 *        JTAG line reset (Test-Logic-Reset) -> read IDCODE of the selected
 *        TAP as a self-test. Does NOT power up the DP.
 */
esp_err_t jtag_connect(void);

/**
 * @brief >=5 TMS-high TCK cycles -> Test-Logic-Reset -> Run-Test/Idle.
 */
esp_err_t jtag_line_reset(void);

/**
 * @brief Chain configuration from DAP_JTAG_Configure.
 * @param count      number of TAPs in the chain
 * @param ir_lengths pointer to @p count IR lengths (bytes)
 */
void jtag_configure(uint8_t count, const uint8_t *ir_lengths);

/**
 * @brief Select which TAP (by chain index) subsequent transfers target.
 */
void jtag_set_device_index(uint8_t index);

/**
 * @brief Number of TAPs configured in the chain (from jtag_configure).
 */
uint8_t jtag_get_count(void);

/* Core I/O (port of ARM JTAG_DP.c) */
void jtag_ir(uint32_t ir);
uint8_t jtag_transfer(uint32_t request, uint32_t *data);
uint32_t jtag_read_idcode(void);
void jtag_write_abort(uint32_t data);
void jtag_sequence(uint32_t info, const uint8_t *tdi, uint8_t *tdo);

/* Raw pins for DAP_SWJ_Pins */
void jtag_pin_tck(bool high);
void jtag_pin_tms(bool high);
void jtag_pin_tdi(bool high);
bool jtag_pin_tdi_in(void);     /* read TDI level back (SWJ_Pins) */
bool jtag_pin_tdo_in(void);
void jtag_pin_ntrst_assert(bool asserted);   /* open-drain, active low */
bool jtag_ntrst_read(void);

#ifdef __cplusplus
}
#endif
