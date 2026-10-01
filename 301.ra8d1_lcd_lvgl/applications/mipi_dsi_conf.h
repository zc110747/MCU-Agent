/**
 * @file mipi_dsi_conf.h
 * @brief MIPI DSI + D-PHY instance configuration for the 2.0" 480x360 panel
 *
 * Phase 5: the GLCDC no longer drives the panel through its parallel RGB
 * pins.  Instead the GLCDC output is bridged into the MIPI DSI host
 * (`phy_layer = &g_mipi_dsi0`) which serialises it over 2 D-PHY lanes to the
 * onboard 2.0" panel.  The panel needs its own DCS power-on sequence, which
 * lives in mipi_dsi_conf.c as `ra8_mipi_lcd_init()`.
 *
 * Every value here is taken verbatim from the official Renesas / RT-Thread
 * board support package application that drives the exact same panel:
 *   projects/lvgl/vision_board_mipi_2.0inch_lvgl  (ra_gen/common_data.c and
 *   board/ports/mipi_lcd/mipi_config.c)
 */
#ifndef MIPI_DSI_CONF_H_
#define MIPI_DSI_CONF_H_

#include "bsp_api.h"

#include "r_mipi_dsi.h"
#include "r_mipi_phy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- geometry */
/* The 2.0" panel is a 480x360 landscape unit; the GLCDC input layer, the DSI
   video timing and the LVGL resolution all have to agree on this. */
#define MIPI_PANEL_WIDTH    (480U)
#define MIPI_PANEL_HEIGHT   (360U)

/* ------------------------------------------------------------------ objects */
/** D-PHY instance (1 GHz PLL, 2 data lanes). */
extern const mipi_phy_instance_t g_mipi_phy0;

/** DSI host instance, referenced by the GLCDC `phy_layer` field. */
extern const mipi_dsi_instance_t g_mipi_dsi0;

/** DSI host control block (shared with the command-table pusher). */
extern mipi_dsi_instance_ctrl_t g_mipi_dsi0_ctrl;

/* -------------------------------------------------------------------- API */
/**
 * Push the panel's DCS power-on / timing table over the DSI command channel.
 * Must be called after R_GLCDC_Open() (which brings the DSI + PHY up) and
 * before R_GLCDC_Start().  Blocking: each command waits for its sequence
 * callback, and the table contains two explicit delays.
 */
void ra8_mipi_lcd_init (void);

/** Number of DCS commands pushed so far (diagnostics). */
uint32_t ra8_mipi_cmd_count (void);

/** SEQ0 (low-power command) interrupt count seen since boot. */
uint32_t ra8_mipi_seq0_count (void);

/** Last PHY status word latched from the DSI callback (diagnostics). */
uint32_t ra8_mipi_phy_status (void);

#ifdef __cplusplus
}
#endif

#endif /* MIPI_DSI_CONF_H_ */
