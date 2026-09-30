/* Peripheral interrupt vector table, Phase 0.5: no IRQ allocated */
#include "bsp_api.h"

#include "vector_data.h"

#if VECTOR_DATA_IRQ_COUNT > 0

BSP_DONT_REMOVE const fsp_vector_t g_vector_table[BSP_IRQ_VECTOR_MAX_ENTRIES]
    BSP_PLACE_IN_SECTION(BSP_SECTION_APPLICATION_VECTORS) = { 0 };

const bsp_interrupt_event_t g_interrupt_event_link_select[BSP_IRQ_VECTOR_MAX_ENTRIES] = { 0 };

#endif /* VECTOR_DATA_IRQ_COUNT > 0 */
