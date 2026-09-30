/* Peripheral interrupt vector table, Phase 1: SCI9 RXI (slot 0)
 *
 * Mirrors the FSP generator output (ra_gen/vector_data.c) so that bsp_irq.c's
 * bsp_irq_cfg() programs IELSR[0] = EVENT_SCI9_RXI at startup.
 */
#include "bsp_api.h"

#include "vector_data.h"

#if VECTOR_DATA_IRQ_COUNT > 0

BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_ICU_VECTOR_MAX_ENTRIES]
    BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) =
{
    [0] = sci_b_uart_rxi_isr, /* SCI9 RXI (Receive data full) */
};

const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_ICU_VECTOR_MAX_ENTRIES] =
{
    [0] = BSP_PRV_IELS_ENUM(EVENT_SCI9_RXI), /* SCI9 RXI (Receive data full) */
};

#endif /* VECTOR_DATA_IRQ_COUNT > 0 */
