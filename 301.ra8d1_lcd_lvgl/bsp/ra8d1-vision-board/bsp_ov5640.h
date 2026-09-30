/**
 * @file bsp_ov5640.h
 * @brief Minimal OV5640 driver for Phase 3 (QVGA RGB565 + colorbar)
 *
 * Strategy: the official OpenMV-derived ov5640.c depends on the whole OMV
 * stack, so this is the distilled version of the exact register path the
 * official camera project executes for QVGA RGB565:
 *   reset() -> default_regs table -> set_pixformat(RGB565) ->
 *   set_framesize(QVGA)
 * with every window value pre-computed from the official formulas
 * (see bsp_ov5640.c for the derivation, all numbers traceable).
 */
#ifndef BSP_OV5640_H_
#define BSP_OV5640_H_

#include "bsp_api.h"

#define BSP_OV5640_ADDR7        (0x36U) /* SCCB slave address (write 0x6C) */
#define BSP_OV5640_CHIP_ID      (0x5640U)

/** Power the module up (PWDN low, RESET high) and leave it ready for SCCB. */
void bsp_ov5640_power_up (void);

/** Raw PWDN/RESET levels (GPIO outputs). Official probe cycles combinations:
 *  some modules invert one of the control lines, so bring-up tries all four. */
void bsp_ov5640_power_levels (bool pwdn_high, bool reset_high);

/** Read the 16-bit chip id (0x5640 on a live OV5640). */
fsp_err_t bsp_ov5640_read_id (uint8_t addr7, uint16_t * p_id);

/**
 * Full bring-up: software reset + default_regs + RGB565 + QVGA window.
 * Same register order as the official project (reset -> pixformat -> framesize).
 */
fsp_err_t bsp_ov5640_init_qvga_rgb565 (uint8_t addr7);

/** Colorbar test pattern (PRE_ISP_TEST 0x503D bit7) - no focus needed. */
fsp_err_t bsp_ov5640_set_colorbar (uint8_t addr7, bool enable);

#endif /* BSP_OV5640_H_ */
