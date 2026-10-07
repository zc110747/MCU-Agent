/* generated-equivalent vector header
 *
 * Phase 1: SCI9 RXI (slot 0)
 * Phase 5: GLCDC LINE DETECT (slot 1) + the six MIPI DSI slots (2..7)
 * Phase 1: DRW INT (slot 8) - the 2D drawing engine the NES blit runs on.
 *
 * Slot numbers are free to assign as long as they are contiguous from 0:
 * bsp_irq.c programs R_ICU->IELSR[i] from g_interrupt_event_link_select[]
 * for every non-zero entry, so slot i == NVIC IRQ i on RA.
 *
 * The CEU capture slot was removed together with the camera stack, and the
 * SCI3 TXI/TEI slots were removed together with the touch panel, so the table
 * is dense: SCI9 / GLCDC / DSI / DRW sit at 0 / 1 / 2..7 / 8.
 */
#ifndef VECTOR_DATA_H
#define VECTOR_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Number of interrupts allocated */
#ifndef VECTOR_DATA_IRQ_COUNT
#define VECTOR_DATA_IRQ_COUNT    (9)
#endif

#if VECTOR_DATA_IRQ_COUNT > 0

/* ISR prototypes.
 *
 * Slots 0 and 1 point at RT-Thread-aware wrappers (bsp_isr.c) rather than the
 * FSP handlers directly: both reach a kernel API (rt_sem_release from the UART
 * callback, nes_port_vsync_notify from VSYNC) and FSP does not bracket its ISRs
 * with rt_interrupt_enter/leave on this build.  The DSI and DRW handlers touch
 * hardware only and are safe to call directly. */
void bsp_uart_rxi_isr(void);
void bsp_glcdc_line_isr(void);
void sci_b_uart_rxi_isr(void);
void glcdc_line_detect_isr(void);
void mipi_dsi_seq0(void);
void mipi_dsi_seq1(void);
void mipi_dsi_vin1(void);
void mipi_dsi_rcv(void);
void mipi_dsi_ferr(void);
void mipi_dsi_ppi(void);
void drw_int_isr(void);

/* Vector table allocations (ICU slot index == NVIC IRQ number on RA).
 * Slot 0 is the debug console UART; slots 1..7 are Phase 5 (GLCDC + DSI).
 * Keep this numbering stable - bsp_irq.c indexes IELSR with the same value,
 * and the DSI slots are referenced by number in mipi_dsi_conf.c. */
#define VECTOR_NUMBER_SCI9_RXI          ((IRQn_Type) 0)  /* SCI9 RXI (Receive data full) */
#define SCI9_RXI_IRQn                   ((IRQn_Type) 0)
#define VECTOR_NUMBER_GLCDC_LINE_DETECT ((IRQn_Type) 1)  /* GLCDC LINE DETECT (vsync) */
#define GLCDC_LINE_DETECT_IRQn          ((IRQn_Type) 1)
#define VECTOR_NUMBER_MIPI_DSI_SEQ0     ((IRQn_Type) 2)  /* DSI SEQ0 (command done) */
#define MIPI_DSI_SEQ0_IRQn              ((IRQn_Type) 2)
#define VECTOR_NUMBER_MIPI_DSI_SEQ1     ((IRQn_Type) 3)  /* DSI SEQ1 */
#define MIPI_DSI_SEQ1_IRQn              ((IRQn_Type) 3)
#define VECTOR_NUMBER_MIPI_DSI_VIN1     ((IRQn_Type) 4)  /* DSI VIN1 */
#define MIPI_DSI_VIN1_IRQn              ((IRQn_Type) 4)
#define VECTOR_NUMBER_MIPI_DSI_RCV      ((IRQn_Type) 5)  /* DSI RCV */
#define MIPI_DSI_RCV_IRQn               ((IRQn_Type) 5)
#define VECTOR_NUMBER_MIPI_DSI_FERR     ((IRQn_Type) 6)  /* DSI FERR */
#define MIPI_DSI_FERR_IRQn              ((IRQn_Type) 6)
#define VECTOR_NUMBER_MIPI_DSI_PPI      ((IRQn_Type) 7)  /* DSI PPI */
#define MIPI_DSI_PPI_IRQn               ((IRQn_Type) 7)
#define VECTOR_NUMBER_DRW_INT           ((IRQn_Type) 8)  /* DRW INT (2D engine) */
#define DRW_INT_IRQn                    ((IRQn_Type) 8)

#endif /* VECTOR_DATA_IRQ_COUNT > 0 */

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_DATA_H */
