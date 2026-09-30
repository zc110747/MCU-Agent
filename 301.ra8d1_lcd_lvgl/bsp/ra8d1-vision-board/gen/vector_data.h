/* generated-equivalent vector header, Phase 1: SCI9 RXI (slot 0),
 * Phase 3: CEU CEUI (slot 1) */
#ifndef VECTOR_DATA_H
#define VECTOR_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Number of interrupts allocated */
#ifndef VECTOR_DATA_IRQ_COUNT
#define VECTOR_DATA_IRQ_COUNT    (2)
#endif

#if VECTOR_DATA_IRQ_COUNT > 0

/* ISR prototypes */
void sci_b_uart_rxi_isr(void);
void ceu_isr(void);

/* Vector table allocations (ICU slot index == NVIC IRQ number on RA) */
#define VECTOR_NUMBER_SCI9_RXI   ((IRQn_Type) 0) /* SCI9 RXI (Receive data full) */
#define SCI9_RXI_IRQn            ((IRQn_Type) 0)
#define VECTOR_NUMBER_CEU_CEUI   ((IRQn_Type) 1) /* CEU CEUI (capture events) */
#define CEU_CEUI_IRQn            ((IRQn_Type) 1)

#endif /* VECTOR_DATA_IRQ_COUNT > 0 */

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_DATA_H */
