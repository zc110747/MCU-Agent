/**
 * @file bsp_irq_cfg.c
 * @brief Interrupt priority constants normally emitted by the FSP generator
 *        into ra_gen/common_data.c.
 *
 * The generated project keeps all of these in one 1000-line file that also
 * carries the GLCDC/DSI/DMAC configuration structs.  This project writes its
 * own BSP, so only the priority constants the upstream FSP modules reference
 * by `extern` are declared here - keeping them next to vector_data.c makes the
 * "slot index == NVIC IRQ number" invariant easy to check.
 */
#include "bsp_api.h"

/* r_drw_irq.c: `extern const uint8_t DRW_INT_IPL;` - used with
   R_BSP_IrqCfgEnable(VECTOR_NUMBER_DRW_INT, DRW_INT_IPL, ...).
   Priority 2 sits above the DSI channels (12) so the drawing engine's
   completion can preempt the display path, and stays above the RT-Thread
   SysTick (kernel uses 0..15 via R_BSP_IrqCfgEnable elsewhere). */
const uint8_t DRW_INT_IPL = (2);
