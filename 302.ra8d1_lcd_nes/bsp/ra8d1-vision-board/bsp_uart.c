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

/* Bounded wait for transmit FIFO space in bsp_uart_putc().
 *
 * At 115200 baud one byte takes ~87 us and the FIFO is 16 deep, so draining a
 * full FIFO takes ~1.4 ms.  The limit below is set far above that: it is a
 * safety valve against a wedged transmitter, not a rate controller.  Sizing it
 * too tight would silently drop characters on any slow drain. */
#define BSP_UART_TX_SPIN_LIMIT    (20000000U)

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

/* TEMP PROBE: how many times the shell has entered the blocking read.  If this
   stays 0 while bytes pile up in the ring, the shell is not reaching its
   input path at all. */
volatile uint32_t g_uart_getchar_calls = 0U;
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

/* Diagnostics for the "shell went deaf" investigation: the callback counts how
   often it runs and records the kernel's interrupt-nesting depth at that
   moment.  If the wrapper in bsp_isr.c is doing its job this must read >= 1;
   a 0 here means rt_sem_release() below believes it is running in thread
   context and will call rt_schedule() from inside the ISR, which is the
   failure this instrumentation exists to confirm. */
volatile uint32_t g_uart_rx_isr_count = 0U;
volatile uint32_t g_uart_rx_isr_nest_last = 0xFFFFFFFFU;
volatile uint32_t g_uart_rx_isr_nest_max = 0U;

static void bsp_uart_callback (uart_callback_args_t * p_args)
{
    if ((NULL != p_args) && (UART_EVENT_RX_CHAR == p_args->event))
    {
        uint32_t next = (g_uart_rx_ring.head + 1U) % BSP_UART_RX_RING_SIZE;

        g_uart_rx_isr_count++;
        g_uart_rx_isr_nest_last = (uint32_t) rt_interrupt_get_nest();
        if (g_uart_rx_isr_nest_last > g_uart_rx_isr_nest_max)
        {
            g_uart_rx_isr_nest_max = g_uart_rx_isr_nest_last;
        }

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
    /* SCI9 runs in FIFO mode (SCI_B_UART_CFG_FIFO_SUPPORT = 1, depth 16), so
       the gate is FIFO occupancy: FTSR_b.T is the number of bytes still queued
       for transmission, and a write is only safe while it is below the depth.
       CSR_b.TDRE is not usable here - in FIFO mode it tracks the
       TDR/shift-register handoff rather than the FIFO, so gating on it overruns
       the FIFO and silently drops characters.
     *
     * The spin is bounded.  A blocking rt_kprintf must never be able to wedge
     * the console permanently: if the transmitter stops draining (TE cleared
     * underneath us, a clock problem, a hardware fault) an unbounded loop would
     * hang whichever thread printed next.  That presents as "the shell went
     * deaf and the log stops mid-line" - the hardest symptom to attribute.  On
     * timeout the byte is dropped and the caller keeps running. */
    if (0U != g_uart9_ctrl.fifo_depth)
    {
        uint32_t spins = 0U;

        while (BSP_UART_REG->FTSR_b.T >= g_uart9_ctrl.fifo_depth)
        {
            if (++spins > BSP_UART_TX_SPIN_LIMIT)
            {
                return;                 /* drop the byte rather than wedge */
            }
        }
    }
    else
    {
        uint32_t spins = 0U;

        while (0U == BSP_UART_REG->CSR_b.TDRE)
        {
            if (++spins > BSP_UART_TX_SPIN_LIMIT)
            {
                return;
            }
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

    for (;;)
    {
        /* Re-check the ring first.  The semaphore is only a wake-up hint: it
           must never be treated as proof that a byte is available.  If a
           spurious release (or a release that raced a reader) leaves the
           count positive while the ring is empty, trusting the semaphore
           makes this function return stale bytes in a tight loop - the shell
           then spins at tens of thousands of iterations per second, never
           blocking, and its input handling breaks in ways that look like a
           dead UART.  Loop on the ring instead. */
        level = rt_hw_interrupt_disable();

        if (g_uart_rx_ring.tail != g_uart_rx_ring.head)
        {
            c = (char) g_uart_rx_ring.buf[g_uart_rx_ring.tail];
            g_uart_rx_ring.tail = (g_uart_rx_ring.tail + 1U) % BSP_UART_RX_RING_SIZE;
            rt_hw_interrupt_enable(level);

            return c;
        }

        rt_hw_interrupt_enable(level);

        /* Nothing buffered: sleep until the ISR reports a byte, then re-check.
           A timeout of 1 tick keeps a lost wake-up from wedging the shell
           forever, at the cost of one idle wake-up per tick. */
        (void) rt_sem_take(&g_uart_rx_sem, 1);
    }
}

uint32_t bsp_uart_rx_pending (void)
{
    uint32_t head = g_uart_rx_ring.head;
    uint32_t tail = g_uart_rx_ring.tail;

    return (head >= tail) ? (head - tail) : (BSP_UART_RX_RING_SIZE + head - tail);
}
