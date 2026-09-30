/**
 * @file bsp_uart.c
 * @brief Debug console UART (SCI9 @ P208/P209) for RA8D1 Vision Board
 *
 * Open path : FSP r_sci_b_uart (channel 9).
 * RX path   : SCI9 RXI interrupt -> UART_EVENT_RX_CHAR -> ring buffer + semaphore.
 * TX path   : polled straight on the SCI register.
 *
 * Why polled TX: rt_hw_console_output() can be called from an ISR or with
 * interrupts disabled, so it must never block on an interrupt or a semaphore.
 * The FSP write API is interrupt-driven and would deadlock there.
 */
#include "bsp_uart.h"

#include <rtthread.h>

#include "bsp_api.h"
#include "r_sci_b_uart.h"
#include "vector_data.h"

#define BSP_UART_CHANNEL          (9U)
#define BSP_UART_RX_RING_SIZE     (64U)
#define BSP_UART_BAUD_ERROR_X1000 (3000U)
#define BSP_UART_IRQ_PRIORITY     (12U)

/* P208 = TXD9, P209 = RXD9. See documents/pinmap.md. */
#define BSP_UART_PIN_TX           BSP_IO_PORT_02_PIN_08
#define BSP_UART_PIN_RX           BSP_IO_PORT_02_PIN_09

/* SCI9 is a SCI_B peripheral: the CMSIS macro R_SCI9 is typed R_SCI0_Type
   (no TDR_BY), so register access goes through the type the FSP driver uses. */
#define BSP_UART_REG              ((R_SCI_B0_Type *) R_SCI9_BASE)

typedef struct
{
    volatile uint32_t head;
    volatile uint32_t tail;
    uint8_t           buf[BSP_UART_RX_RING_SIZE];
} bsp_uart_ring_t;

/* P208/P209 are muxed by bsp_pin_init(). This driver only opens the SCI9
   peripheral: IOPORT is opened exactly once, from bsp_pin_init(). */
static sci_b_uart_instance_ctrl_t g_uart9_ctrl;
static sci_b_baud_setting_t       g_uart9_baud_setting;
static bsp_uart_ring_t            g_uart_rx_ring;
static struct rt_semaphore        g_uart_rx_sem;

static void bsp_uart_callback(uart_callback_args_t * p_args);

static const sci_b_uart_extended_cfg_t g_uart9_cfg_extend =
{
    .clock            = SCI_B_UART_CLOCK_INT,
    .rx_edge_start    = SCI_B_UART_START_BIT_FALLING_EDGE,
    .noise_cancel     = SCI_B_UART_NOISE_CANCELLATION_DISABLE,
    .rx_fifo_trigger  = SCI_B_UART_RX_FIFO_TRIGGER_MAX,
    .p_baud_setting   = &g_uart9_baud_setting,
    .flow_control     = SCI_B_UART_FLOW_CONTROL_RTS,
    .flow_control_pin = (bsp_io_port_pin_t) UINT16_MAX,
    .rs485_setting    =
    {
        .enable         = SCI_B_UART_RS485_DISABLE,
        .polarity       = SCI_B_UART_RS485_DE_POLARITY_HIGH,
        .assertion_time = 1,
        .negation_time  = 1,
    },
};

static const uart_cfg_t g_uart9_cfg =
{
    .channel        = BSP_UART_CHANNEL,
    .data_bits      = UART_DATA_BITS_8,
    .parity         = UART_PARITY_OFF,
    .stop_bits      = UART_STOP_BITS_1,
    .rxi_ipl        = BSP_UART_IRQ_PRIORITY,
    .rxi_irq        = VECTOR_NUMBER_SCI9_RXI,
    .txi_ipl        = BSP_UART_IRQ_PRIORITY,
    .txi_irq        = FSP_INVALID_VECTOR,
    .tei_ipl        = BSP_UART_IRQ_PRIORITY,
    .tei_irq        = FSP_INVALID_VECTOR,
    .eri_ipl        = BSP_UART_IRQ_PRIORITY,
    .eri_irq        = FSP_INVALID_VECTOR,
    .p_transfer_rx  = NULL,
    .p_transfer_tx  = NULL,
    .p_callback     = bsp_uart_callback,
    .p_context      = NULL,
    .p_extend       = &g_uart9_cfg_extend,
};

static void bsp_uart_callback (uart_callback_args_t * p_args)
{
    if ((NULL != p_args) && (UART_EVENT_RX_CHAR == p_args->event))
    {
        uint32_t next = (g_uart_rx_ring.head + 1U) % BSP_UART_RX_RING_SIZE;

        /* Drop on overflow: the ISR must not block. */
        if (next != g_uart_rx_ring.tail)
        {
            g_uart_rx_ring.buf[g_uart_rx_ring.head] = (uint8_t) p_args->data;
            g_uart_rx_ring.head = next;
            (void) rt_sem_release(&g_uart_rx_sem);
        }
    }
}

void bsp_uart_init (void)
{
    fsp_err_t err;

    err = R_SCI_B_UART_BaudCalculate(BSP_UART_BAUDRATE,
                                     false,
                                     BSP_UART_BAUD_ERROR_X1000,
                                     &g_uart9_baud_setting);
    if (FSP_SUCCESS != err)
    {
        return;
    }

    (void) rt_sem_init(&g_uart_rx_sem, "uart9rx", 0, RT_IPC_FLAG_FIFO);

    err = R_SCI_B_UART_Open(&g_uart9_ctrl, &g_uart9_cfg);
    if (FSP_SUCCESS != err)
    {
        return;
    }
}

void bsp_uart_putc (char c)
{
    uint32_t fifo_depth = g_uart9_ctrl.fifo_depth;

    /* SCI9 runs in FIFO mode (SCI_B_UART_CFG_FIFO_SUPPORT = 1, depth 16). In
       FIFO mode CSR_b.TDRE only tracks the TDR/shift-register handoff, so a
       putc loop gated on TDRE overruns the FIFO and silently drops characters.
       Gate on the FIFO data count instead - that is what the driver's own TXI
       ISR does (r_sci_b_uart.c, FTSR_b.T vs p_ctrl->fifo_depth). */
    if (0U != fifo_depth)
    {
        while (fifo_depth <= BSP_UART_REG->FTSR_b.T)
        {
            ; /* wait for a free slot in the transmit FIFO */
        }
    }
    else
    {
        while (0U == BSP_UART_REG->CSR_b.TDRE)
        {
            ; /* non-FIFO fallback: wait for the transmit data register to drain */
        }
    }

    BSP_UART_REG->TDR_BY = (uint8_t) c;
}

void bsp_uart_write (const char * s, uint32_t len)
{
    uint32_t i;

    for (i = 0U; i < len; i++)
    {
        if ('\n' == s[i])
        {
            bsp_uart_putc('\r');
        }

        bsp_uart_putc(s[i]);
    }
}

char bsp_uart_getchar (void)
{
    rt_base_t level;
    char      c;

    (void) rt_sem_take(&g_uart_rx_sem, RT_WAITING_FOREVER);

    level = rt_hw_interrupt_disable();
    c = (char) g_uart_rx_ring.buf[g_uart_rx_ring.tail];
    g_uart_rx_ring.tail = (g_uart_rx_ring.tail + 1U) % BSP_UART_RX_RING_SIZE;
    rt_hw_interrupt_enable(level);

    return c;
}

uint32_t bsp_uart_rx_pending (void)
{
    uint32_t head = g_uart_rx_ring.head;
    uint32_t tail = g_uart_rx_ring.tail;

    return (head >= tail) ? (head - tail) : (BSP_UART_RX_RING_SIZE + head - tail);
}
