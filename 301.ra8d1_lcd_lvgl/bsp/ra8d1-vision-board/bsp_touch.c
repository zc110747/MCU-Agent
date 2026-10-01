/**
 * @file bsp_touch.c
 * @brief CST812T capacitive touch controller over SCI3 I2C (FSP native)
 *
 * Which chip, and why the pins matter:
 *   The panel fitted to this board is the MIPI DSI 2.0" (480x360) unit, whose
 *   controller is a Hynitron CST812T.  Two facts had to be pinned down from
 *   the schematic and the official projects, and both were wrong in the first
 *   cut of this file:
 *
 *   1. Bus pins.  The CST812T is NOT on P208/P209.  Those are the SCI9 debug
 *      console.  The schematic (Vision_Board_schematic.pdf, DISPLAY CTRL
 *      block) traces the panel nets CTP_SCL/CTP_SDA back to MCU nets
 *      SCL0/SDA0, i.e. P408/P409.  P408/P409 additionally need
 *      IOPORT_CFG_NMOS_ENABLE (the panel side of that bus is level-shifted).
 *      Both pin pairs sit in the same IOPORT PSEL group (SCI1_3_5_7_9), which
 *      is what made the two easy to confuse - see bsp_pin.c Phase 6.
 *   2. Chip.  The 4.3" RGB variant uses a Goodix GT9147 on a *different*
 *      register model, so the two drivers are not interchangeable.
 *
 * Transfer model:
 *   The 8-bit register index is written, then the payload is read after a
 *   repeated START, i.e.
 *     write(&reg, 1, restart=true)  ->  read(buf, len, restart=false)
 *   The FSP i2c_master API is asynchronous and ALWAYS returns FSP_SUCCESS
 *   from read()/write(); a slave NACK shows up only as an
 *   I2C_MASTER_EVENT_ABORTED in the completion callback.  Each call therefore
 *   blocks on a semaphore and the recorded event decides success - without
 *   this a dead bus reads back as "success" with a buffer full of zeros.
 *
 * Chip quirks worth remembering:
 *   - Register 0x02 (finger number) is read as a 5-byte burst covering
 *     0x02..0x06, i.e. finger count + x[11:8]x[7:0] + y[11:8]y[7:0].
 *   - Only the low nibble of the finger count is meaningful.
 *   - After reset the chip needs a settling delay before it answers on I2C.
 *     cst8xx_reset(20) matches the official driver.
 */
#include "bsp_touch.h"

#include <rtthread.h>

#include "app_config.h"

#if APP_ENABLE_TOUCH

#include "bsp_pin.h"
#include "r_i2c_master_api.h"
#include "r_ioport.h"
#include "r_sci_b_i2c.h"
#include "vector_data.h"

/* SCI3 is the bus the touch controller hangs off.  The FSP instance is built
   here rather than in a generated ra_gen/hal_data.c, because this project has
   no FSP configurator output: every peripheral instance is hand-written next
   to the driver that uses it (same as bsp_uart.c's SCI9). */
#define BSP_TOUCH_CHANNEL          (3U)
#define BSP_TOUCH_SLAVE_ADDR       (0x15U)   /* CST812T application mode */
#define BSP_TOUCH_IRQ_PRIORITY     (12U)
#define BSP_TOUCH_TIMEOUT_MS       (20U)

/* CST812T register map (only what this driver touches). */
#define CST8XX_REG_FINGER_NUM      (0x02U)   /* 5-byte burst: 0x02..0x06 */
#define CST8XX_REG_FW_VERSION      (0xA9U)

#define CST8XX_FINGER_MASK         (0x0FU)   /* low nibble = finger count */
#define CST8XX_BURST_LEN           (5U)

/* Settling delays for the reset sequence, from the official driver. */
#define CST8XX_RESET_LOW_MS        (20U)
#define CST8XX_RESET_SETTLE_MS     (10U)
#define CST8XX_PROBE_SETTLE_MS     (50U)

/* The probe is retried: a NACKed address leaves the SCI_B block parked and a
   cold CST812T occasionally misses the first frame. */
#define CST8XX_PROBE_ATTEMPTS      (5U)
#define CST8XX_PROBE_RETRY_MS      (20U)

/* Clock settings verbatim from the official ra_gen/hal_data.c: ~100 kHz off
   PCLK (actual 99.992 kHz, SDA delay 258.33 ns). */
#define BSP_TOUCH_CLOCK_DIVISOR    (0U)
#define BSP_TOUCH_BRR_VALUE        (22U)
#define BSP_TOUCH_MDDR_VALUE       (157U)
#define BSP_TOUCH_CYCLES_VALUE     (31U)
#define BSP_TOUCH_SNFR_VALUE       (1U)

/* ---- objects ------------------------------------------------------------- */
static sci_b_i2c_instance_ctrl_t g_touch_ctrl;
static struct rt_semaphore       g_touch_sem;
static volatile bool             g_touch_ready;

/* Last event reported by the FSP completion callback.  R_SCI_B_I2C_Read/Write
   are asynchronous and ALWAYS return FSP_SUCCESS; a slave NACK surfaces only
   here, as I2C_MASTER_EVENT_ABORTED.  Without this the driver would treat a
   dead bus as a successful read of zeroed buffer contents. */
static volatile i2c_master_event_t g_touch_event;

/* Counts how many times the FSP completion callback actually fired.  Used to
   distinguish "ISR never ran" (counter 0, true semaphore timeout) from "ISR
   ran but the slave NACKed" (counter > 0, ABORTED event). */
static volatile uint32_t g_touch_cb_count;

/* Forensic snapshots taken inside the completion callback, read back over SWD.
   g_touch_isr_snap is the SCI3 ISR value at each event; [0] is the first event
   (address phase), [1] the second (data phase).  Letting us see exactly which
   phase NACKs without a scope. */
volatile uint32_t g_touch_isr_snap[4];
volatile uint32_t g_touch_csr_snap[4];
volatile uint32_t g_touch_icr_snap[4];

static void bsp_touch_callback (i2c_master_callback_args_t * p_args);

static const sci_b_i2c_extended_cfg_t g_touch_cfg_extend =
{
    .clock_settings =
    {
        .bitrate_modulation = true,
        .brr_value          = BSP_TOUCH_BRR_VALUE,
        .clk_divisor_value  = BSP_TOUCH_CLOCK_DIVISOR,
        .mddr_value         = BSP_TOUCH_MDDR_VALUE,
        .cycles_value       = BSP_TOUCH_CYCLES_VALUE,
        .snfr_value         = BSP_TOUCH_SNFR_VALUE,
        .clock_source       = SCI_B_I2C_CLOCK_SOURCE_PCLK,
    },
};

static const i2c_master_cfg_t g_touch_cfg =
{
    .channel        = BSP_TOUCH_CHANNEL,
    .rate           = I2C_MASTER_RATE_STANDARD,
    .slave          = BSP_TOUCH_SLAVE_ADDR,
    .addr_mode      = I2C_MASTER_ADDR_MODE_7BIT,
    .ipl            = BSP_TOUCH_IRQ_PRIORITY,
    /* SCI3 needs both TXI and TEI as REAL vector numbers: r_sci_b_i2c.c calls
       R_BSP_IrqCfgEnable(p_cfg->txi_irq, ...) / (tei_irq, ...) unconditionally
       during open, and FSP_INVALID_VECTOR is (IRQn_Type)-33, which the NVIC
       helper would turn into an out-of-bounds NVIC->ISER index.  RXI is only
       used when DTC is enabled (it is not), so it stays invalid. */
    .rxi_irq        = FSP_INVALID_VECTOR,
    .txi_irq        = VECTOR_NUMBER_SCI3_TXI,
    .tei_irq        = VECTOR_NUMBER_SCI3_TEI,
    .eri_irq        = FSP_INVALID_VECTOR,
    .p_transfer_tx  = NULL,
    .p_transfer_rx  = NULL,
    .p_callback     = bsp_touch_callback,
    .p_context      = NULL,
    .p_extend       = &g_touch_cfg_extend,
};

static const i2c_master_instance_t g_touch_instance =
{
    .p_ctrl = &g_touch_ctrl,
    .p_cfg  = &g_touch_cfg,
    .p_api  = &g_i2c_master_on_sci_b,
};

/* ---- transfer plumbing --------------------------------------------------- */

static void bsp_touch_callback (i2c_master_callback_args_t * p_args)
{
    /* Record the event before posting: the waiter reads it after the take(),
       and the ISR may already be queueing the next transfer's event. */
    g_touch_event = p_args->event;
    g_touch_cb_count++;

    /* Forensic snapshot (see declaration): capture the raw SCI3 status the
       moment each completion event fires, so a NACK can be attributed to the
       address byte vs. the data byte without a logic analyser. */
    if (g_touch_cb_count <= 4U)
    {
        uint32_t idx = g_touch_cb_count - 1U;
        g_touch_isr_snap[idx] = R_SCI_B3->ISR;
        g_touch_csr_snap[idx] = R_SCI_B3->CSR;
        g_touch_icr_snap[idx] = R_SCI_B3->ICR;
    }

    /* Posts from an ISR context: rt_sem_release is interrupt-safe. */
    (void) rt_sem_release(&g_touch_sem);
}

/* Check the physical state of the two I2C lines.  Muxes SCL/SDA to plain
   inputs (no pull-up) and reads them: a healthy idle bus shows both HIGH via
   the board pull-ups (R66 SCL and R67 SDA, 10k to +5V_SYS on the MIPI 2.0"
   panel).  A line held LOW with both the SCI driver and the board pull-up
   unable to raise it is a hard short on that net - no amount of retrying can
   make the slave answer, so it is worth reporting distinctly from a NACK.

   Returns a two-bit mask: bit0 = SCL low, bit1 = SDA low (0 = both high). */
static uint32_t bsp_touch_bus_health (void)
{
    bsp_io_level_t scl;
    bsp_io_level_t sda;
    uint32_t       mask = 0U;

    /* Drive both lines to inputs (direction in); the pull-ups are on the
       board, so no internal pull is needed. */
    (void) bsp_pin_cfg(BSP_TOUCH_PIN_SCL, (uint32_t) IOPORT_CFG_PORT_DIRECTION_INPUT);
    (void) bsp_pin_cfg(BSP_TOUCH_PIN_SDA, (uint32_t) IOPORT_CFG_PORT_DIRECTION_INPUT);
    rt_thread_mdelay(1);

    if (FSP_SUCCESS == bsp_pin_read(BSP_TOUCH_PIN_SCL, &scl) && (BSP_IO_LEVEL_LOW == scl))
    {
        mask |= 1U;
    }
    if (FSP_SUCCESS == bsp_pin_read(BSP_TOUCH_PIN_SDA, &sda) && (BSP_IO_LEVEL_LOW == sda))
    {
        mask |= 2U;
    }

    /* Put the peripheral mux back: the SCI3 block owns the pins. */
    (void) bsp_pin_cfg(BSP_TOUCH_PIN_SCL,
                       (uint32_t) IOPORT_CFG_PERIPHERAL_PIN | (uint32_t) IOPORT_PERIPHERAL_SCI1_3_5_7_9);
    (void) bsp_pin_cfg(BSP_TOUCH_PIN_SDA,
                       (uint32_t) IOPORT_CFG_PERIPHERAL_PIN | (uint32_t) IOPORT_PERIPHERAL_SCI1_3_5_7_9);

    return mask;
}

/* Live dump of the SCI3 I2C block to tell a true semaphore timeout apart from a
   bus-level NACK.  Called from the probe-failure path. */
static void bsp_touch_dump_sci3 (void)
{
    volatile R_SCI_B0_Type * p = R_SCI_B3;
    uint32_t ccr0 = p->CCR0;
    uint32_t icr  = p->ICR;
    uint32_t isr  = p->ISR;
    uint32_t bus  = bsp_touch_bus_health();

    rt_kprintf("[touch] SCI3 CCR0=0x%08X ICR=0x%08X ISR=0x%08X\n",
               (unsigned int) ccr0, (unsigned int) icr, (unsigned int) isr);
    rt_kprintf("[touch]   CCR0.TE=%d RE=%d  ICR.STAREQ=%d  ISR.NACK=%d STIF=%d\n",
               (int) ((ccr0 >> 4) & 1U), (int) (ccr0 & 1U),
               (int) ((icr >> 16) & 1U), (int) (isr & 1U), (int) ((isr >> 3) & 1U));
    rt_kprintf("[touch]   g_touch_event=0x%02X cb_count=%u (%s)\n",
               (unsigned int) g_touch_event, (unsigned int) g_touch_cb_count,
               (g_touch_event == I2C_MASTER_EVENT_ABORTED) ? "ABORTED/NACK" : "other");
    rt_kprintf("[touch]   bus lines: SCL(P408)=%s SDA(P409)=%s%s\n",
               ((bus & 1U) ? "LOW " : "high"), ((bus & 2U) ? "LOW " : "high"),
               (0U != bus) ? "  <-- line stuck low: check panel FPC / wiring" : "");
}

/* Bounded wait for one FSP transfer to complete.  Returns false on timeout so
   a missing/stuck controller degrades into "no touch" rather than a hang.
   Also rejects I2C_MASTER_EVENT_ABORTED: that is how the FSP reports a NACK
   or a bus error, and the byte buffer is left untouched in that case. */
static bool touch_wait (void)
{
    if (RT_EOK != rt_sem_take(&g_touch_sem,
                              rt_tick_from_millisecond(BSP_TOUCH_TIMEOUT_MS)))
    {
        return false;
    }

    return (I2C_MASTER_EVENT_ABORTED != g_touch_event);
}

/* Start one transfer and wait for its completion event.  A transfer that ends
   in I2C_MASTER_EVENT_ABORTED (NACK / arbitration loss) leaves the SCI_B I2C
   block driving SCL low, which strands every following transfer.  Call abort()
   to force the block back to idle before giving up. */
static fsp_err_t touch_run (fsp_err_t err)
{
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    if (touch_wait())
    {
        return FSP_SUCCESS;
    }

    (void) g_touch_instance.p_api->abort(g_touch_instance.p_ctrl);

    return FSP_ERR_TIMEOUT;
}

/* Read `len` bytes starting at 8-bit register `reg`. */
static fsp_err_t touch_read_reg (uint8_t reg, uint8_t * buf, uint32_t len)
{
    fsp_err_t err;

    g_touch_event = I2C_MASTER_EVENT_ABORTED;   /* pessimistic default */
    err = touch_run(g_touch_instance.p_api->write(g_touch_instance.p_ctrl,
                                                  &reg, 1U, true));
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_touch_event = I2C_MASTER_EVENT_ABORTED;
    return touch_run(g_touch_instance.p_api->read(g_touch_instance.p_ctrl,
                                                  buf, len, false));
}

/* ---- reset / probe ------------------------------------------------------- */

static void touch_hw_reset (void)
{
    /* RST is active low.  Blocking mdelay keeps the pulse width exact. */
    (void) bsp_pin_write(BSP_TOUCH_PIN_RST, BSP_IO_LEVEL_LOW);
    rt_thread_mdelay(CST8XX_RESET_LOW_MS);
    (void) bsp_pin_write(BSP_TOUCH_PIN_RST, BSP_IO_LEVEL_HIGH);
    rt_thread_mdelay(CST8XX_RESET_SETTLE_MS);
}

/* The prebuilt FSP library's bsp_irq_cfg() was compiled with a vector-table
   limit of 8, so it only walks IELSR[0..7].  Our SCI3 touch IRQs sit at
   IELSR[8] (TXI) and [9] (TEI); they were therefore left unmapped, so the FSP
   I2C transfer never reached its completion ISR and every read timed out
   (probe failure 0x14 = FSP_ERR_TIMEOUT).  Replicate exactly what bsp_irq_cfg()
   does for the lower indices: copy the ELC event out of the vector-link table
   into IELSR.  No PRC1 unlock is needed - bsp_irq_cfg() itself writes IELSR
   without one, and the display interrupts (slots 1..7) prove those writes take
   effect.  If more IRQs beyond slot 7 are ever added, extend this or rebuild
   the FSP library with the correct BSP_VECTOR_TABLE_MAX_ENTRIES. */
extern const bsp_interrupt_event_t g_interrupt_event_link_select[];

static void bsp_touch_irq_link_fixup (void)
{
    R_ICU->IELSR[VECTOR_NUMBER_SCI3_TXI] =
        (uint32_t) g_interrupt_event_link_select[VECTOR_NUMBER_SCI3_TXI];
    R_ICU->IELSR[VECTOR_NUMBER_SCI3_TEI] =
        (uint32_t) g_interrupt_event_link_select[VECTOR_NUMBER_SCI3_TEI];
}

fsp_err_t bsp_touch_init (void)
{
    fsp_err_t err;
    uint8_t   fw = 0U;

    if (g_touch_ready)
    {
        return FSP_SUCCESS;
    }

    /* Map SCI3 TXI/TEI into the ICU before the driver opens the bus, otherwise
       the transfer completion ISR never fires and the probe times out. */
    bsp_touch_irq_link_fixup();

    if (RT_EOK != rt_sem_init(&g_touch_sem, "touch", 0, RT_IPC_FLAG_PRIO))
    {
        return FSP_ERR_INTERNAL;
    }

    /* RST as an output idling high; touch_hw_reset() drives the pulse.
       INT stays the plain input bsp_pin_init() configured: this driver polls,
       so no IRQ attachment is needed (LVGL reads periodically). */
    (void) bsp_pin_cfg(BSP_TOUCH_PIN_RST,
                       (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                       (uint32_t) IOPORT_CFG_PORT_OUTPUT_HIGH);

    err = g_touch_instance.p_api->open(g_touch_instance.p_ctrl, g_touch_instance.p_cfg);
    if (FSP_SUCCESS != err)
    {
        rt_kprintf("[touch] I2C open failed: 0x%x\n", (unsigned int) err);
        return err;
    }

    touch_hw_reset();
    rt_thread_mdelay(CST8XX_PROBE_SETTLE_MS);

    /* Probe: the firmware-version register is a constant the chip always
       answers, so a successful read proves both the bus and the address.
       Retry a few times: the CST812T is known to miss the very first frame
       after a cold reset on some panel revisions, and a NACKed address leaves
       the SCI_B block needing an abort() before it will start again. */
    for (uint32_t attempt = 0U; attempt < CST8XX_PROBE_ATTEMPTS; attempt++)
    {
        err = touch_read_reg(CST8XX_REG_FW_VERSION, &fw, 1U);
        if (FSP_SUCCESS == err)
        {
            break;
        }
        (void) g_touch_instance.p_api->abort(g_touch_instance.p_ctrl);
        rt_thread_mdelay(CST8XX_PROBE_RETRY_MS);
    }

    if (FSP_SUCCESS != err)
    {
        bsp_touch_dump_sci3();
        rt_kprintf("[touch] CST812T probe failed: 0x%x (no chip at 0x%02x on SCI3 P408/P409?)\n",
                   (unsigned int) err, (unsigned int) BSP_TOUCH_SLAVE_ADDR);
        return err;
    }

    g_touch_ready = true;
    rt_kprintf("[touch] CST812T up, fw=0x%02x addr=0x%02x (SCL=P408/SDA=P409, RST=P000)\n",
               (unsigned int) fw, (unsigned int) BSP_TOUCH_SLAVE_ADDR);

    return FSP_SUCCESS;
}

fsp_err_t bsp_touch_read (bsp_touch_point_t * p_point)
{
    uint8_t   buf[CST8XX_BURST_LEN] = {0};
    fsp_err_t err;

    if ((NULL == p_point) || (!g_touch_ready))
    {
        return FSP_ERR_NOT_OPEN;
    }

    err = touch_read_reg(CST8XX_REG_FINGER_NUM, buf, CST8XX_BURST_LEN);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    p_point->pressed = (0U != (buf[0] & CST8XX_FINGER_MASK));
    p_point->x       = (uint16_t) ((((uint16_t) (buf[1] & 0x0FU)) << 8) | (uint16_t) buf[2]);
    p_point->y       = (uint16_t) ((((uint16_t) (buf[3] & 0x0FU)) << 8) | (uint16_t) buf[4]);

    return FSP_SUCCESS;
}

bool bsp_touch_ready (void)
{
    return g_touch_ready;
}

uint8_t bsp_touch_fw_version (void)
{
    uint8_t fw = 0U;

    if (!g_touch_ready)
    {
        return 0U;
    }

    if (FSP_SUCCESS != touch_read_reg(CST8XX_REG_FW_VERSION, &fw, 1U))
    {
        return 0U;
    }

    return fw;
}

fsp_err_t bsp_touch_probe_addr (uint8_t addr)
{
    fsp_err_t err;
    uint8_t   reg = CST8XX_REG_FW_VERSION;

    /* Retarget the controller at the address under test.  A single write of
       one byte is enough: the slave either ACKs its address byte or it does
       not, and the FSP reports the latter as I2C_MASTER_EVENT_ABORTED. */
    err = g_touch_instance.p_api->slaveAddressSet(g_touch_instance.p_ctrl, addr,
                                                  I2C_MASTER_ADDR_MODE_7BIT);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_touch_event = I2C_MASTER_EVENT_ABORTED;
    err = touch_run(g_touch_instance.p_api->write(g_touch_instance.p_ctrl,
                                                  &reg, 1U, false));

    /* Put the configured address back so a scan never leaves the driver
       pointing at a stray slave. */
    (void) g_touch_instance.p_api->slaveAddressSet(g_touch_instance.p_ctrl,
                                                   BSP_TOUCH_SLAVE_ADDR,
                                                   I2C_MASTER_ADDR_MODE_7BIT);

    return err;
}

/* Read the firmware-version register and the raw finger-count byte.  Used by
   the console to show what actually answered on the bus. */
fsp_err_t bsp_touch_product_id (uint8_t * buf, uint32_t len)
{
    if (NULL == buf)
    {
        return FSP_ERR_ASSERTION;
    }
    if (len > 1U)
    {
        len = 1U;
    }

    return touch_read_reg(CST8XX_REG_FW_VERSION, buf, len);
}

uint8_t bsp_touch_status_raw (void)
{
    uint8_t status = 0U;

    if (FSP_SUCCESS != touch_read_reg(CST8XX_REG_FINGER_NUM, &status, 1U))
    {
        return 0xFFU;
    }

    return status;
}

#endif /* APP_ENABLE_TOUCH */
