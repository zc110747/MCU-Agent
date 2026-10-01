/* Peripheral interrupt vector table
 *
 * Phase 1: SCI9 RXI (slot 0)
 * Phase 5: GLCDC LINE DETECT (slot 1) + the six MIPI DSI slots (2..7)
 *
 * Mirrors the FSP generator output (ra_gen/vector_data.c): bsp_irq_cfg()
 * walks g_interrupt_event_link_select[] and programs IELSR[i] for every
 * non-zero entry, so the slot index is also the NVIC IRQ number.
 *
 * Slot 1 carries the GLCDC line-detect interrupt, which the DSI video mode
 * uses as its frame boundary (LVGL waits on it to avoid tearing).  Slots 2..7
 * are the DSI sequence / error channels; SEQ0 is mandatory because the DCS
 * command pusher blocks on its completion callback.
 *
 * The CEU capture slot this table used to carry at index 1 was removed
 * together with the camera stack (r_ceu dropped from the build, bsp_cam /
 * bsp_sccb / bsp_ov5640 deleted), and the SCI3 TXI/TEI slots were removed
 * with the touch panel in Phase 7.  Slots are renumbered densely from 0
 * because bsp_irq_cfg() only programs the entries the array provides and the
 * DSI slots carry their own explicit IRQ numbers in mipi_dsi_conf.c -- an
 * empty hole would work, but a dense table keeps slot i == IRQ i obvious.
 */
#include "bsp_api.h"

#include "vector_data.h"

#if VECTOR_DATA_IRQ_COUNT > 0

BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_MAX_ENTRIES]
    BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
{
    [0] = sci_b_uart_rxi_isr,   /* SCI9 RXI (Receive data full)            */
    [1] = glcdc_line_detect_isr,/* GLCDC LINE DETECT (vsync / frame start) */
    [2] = mipi_dsi_seq0,        /* DSI SEQ0 (command sequence finished)    */
    [3] = mipi_dsi_seq1,        /* DSI SEQ1                                */
    [4] = mipi_dsi_vin1,        /* DSI VIN1                                */
    [5] = mipi_dsi_rcv,         /* DSI RCV                                 */
    [6] = mipi_dsi_ferr,        /* DSI FERR                                */
    [7] = mipi_dsi_ppi,         /* DSI PPI                                 */
};

const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_MAX_ENTRIES] =
{
    [0] = BSP_PRV_IELS_ENUM(EVENT_SCI9_RXI),           /* SCI9 RXI                */
    [1] = BSP_PRV_IELS_ENUM(EVENT_GLCDC_LINE_DETECT),  /* GLCDC LINE DETECT       */
    [2] = BSP_PRV_IELS_ENUM(EVENT_MIPI_DSI_SEQ0),      /* DSI SEQ0                */
    [3] = BSP_PRV_IELS_ENUM(EVENT_MIPI_DSI_SEQ1),      /* DSI SEQ1                */
    [4] = BSP_PRV_IELS_ENUM(EVENT_MIPI_DSI_VIN1),      /* DSI VIN1                */
    [5] = BSP_PRV_IELS_ENUM(EVENT_MIPI_DSI_RCV),       /* DSI RCV                 */
    [6] = BSP_PRV_IELS_ENUM(EVENT_MIPI_DSI_FERR),      /* DSI FERR                */
    [7] = BSP_PRV_IELS_ENUM(EVENT_MIPI_DSI_PPI),       /* DSI PPI                 */
};

#endif /* VECTOR_DATA_IRQ_COUNT > 0 */
