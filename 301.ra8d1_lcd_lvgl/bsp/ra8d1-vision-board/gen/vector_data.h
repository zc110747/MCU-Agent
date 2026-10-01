/* generated-equivalent vector header
 *
 * Phase 1: SCI9 RXI (slot 0)
 * Phase 5: GLCDC LINE DETECT (slot 1) + the six MIPI DSI slots (2..7)
 * Phase 6: SCI3 TXI / TEI (slots 8/9) for the CST812T touch I2C bus
 *
 * Slot numbers are free to assign as long as they are contiguous from 0:
 * bsp_irq.c programs R_ICU->IELSR[i] from g_interrupt_event_link_select[]
 * for every non-zero entry, so slot i == NVIC IRQ i on RA.
 *
 * The CEU capture slot was removed together with the camera stack; the table
 * is now dense, so the SCI / GLCDC / DSI / SCI3 entries sit at 0 / 1 / 2..7 / 8..9.
 *
 * NOTE: SCI3 TXI and TEI are NOT optional.  r_sci_b_i2c.c unconditionally
 * calls R_BSP_IrqCfgEnable(p_cfg->txi_irq, ...) and (tei_irq, ...) during
 * open; handing it FSP_INVALID_VECTOR would compute
 * NVIC->ISER[(uint32_t)(-33) >> 5] and write far outside the NVIC register
 * block.  The official board allocates exactly the same two SCI3 slots.
 */
#ifndef VECTOR_DATA_H
#define VECTOR_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Number of interrupts allocated */
#ifndef VECTOR_DATA_IRQ_COUNT
#define VECTOR_DATA_IRQ_COUNT    (10)
#endif

#if VECTOR_DATA_IRQ_COUNT > 0

/* ISR prototypes */
void sci_b_uart_rxi_isr(void);
void glcdc_line_detect_isr(void);
void mipi_dsi_seq0(void);
void mipi_dsi_seq1(void);
void mipi_dsi_vin1(void);
void mipi_dsi_rcv(void);
void mipi_dsi_ferr(void);
void mipi_dsi_ppi(void);
void sci_b_i2c_txi_isr(void);
void sci_b_i2c_tei_isr(void);

/* Vector table allocations (ICU slot index == NVIC IRQ number on RA).
 * Slot 0 is the debug console UART; slots 1..7 are Phase 5 (GLCDC + DSI);
 * slots 8/9 are the Phase 6 touch I2C.  Keep this numbering stable -
 * bsp_irq.c indexes IELSR with the same value, and the DSI slots are
 * referenced by number in mipi_dsi_conf.c. */
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
#define VECTOR_NUMBER_SCI3_TXI          ((IRQn_Type) 8)  /* SCI3 TXI (touch I2C TX) */
#define SCI3_TXI_IRQn                   ((IRQn_Type) 8)
#define VECTOR_NUMBER_SCI3_TEI          ((IRQn_Type) 9)  /* SCI3 TEI (touch I2C TX end) */
#define SCI3_TEI_IRQn                   ((IRQn_Type) 9)

#endif /* VECTOR_DATA_IRQ_COUNT > 0 */

#ifdef __cplusplus
}
#endif

#endif /* VECTOR_DATA_H */
