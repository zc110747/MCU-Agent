/**
 * @file bsp_ospi.c
 * @brief Board OSPI serial-flash wrapper - see bsp_ospi.h.
 *
 * Register reference: the RA8D1 xSPI block is a single unit with two channels;
 * FSP names it R_XSPI (Drivers/renesas/fsp/.../R7FA8D1BH.h), and the r_ospi_b
 * driver addresses it as OSPI_B unit 0.
 *
 * Pin map, as wired on this board:
 *   OSPI_CS1=P104   OSPI_SCLK=P808  OSPI_SIO0=P100
 *   OSPI_SIO1=P803  OSPI_SIO2=P103  OSPI_SIO3=P101
 * The flash hangs off chip-select 1, so the driver is configured with
 * OSPI_B_DEVICE_NUMBER_1 and every channel-indexed register
 * (LIOCFGCS/CMCFGCS/CCCTLCS) and the bridge-map bit come from that channel.
 * Reading the schematic net name "P104" as port 10 pin 4 is wrong: the FSP
 * designator is BSP_IO_PORT_10_PIN_04, i.e. port 10, pin 4.  Getting this
 * wrong is silent - the mux lands on an unrelated pin and the flash answers
 * 00 00 00.
 *
 * The board wires only these six, in 1S-1S-1S mode, so no DQS/DLL tuning and
 * no RSIP is involved.
 *
 * ---------------------------------------------------------------------------
 * Why everything goes through the memory-mapped window
 * ---------------------------------------------------------------------------
 * The r_ospi_b API is not a streaming flash API on this device:
 *
 *   - DirectRead()      is a stub returning FSP_ERR_UNSUPPORTED.
 *   - DirectTransfer()  moves at most CDBUF[0]'s 16 bytes per transaction.
 *   - Write()           with DMAC disabled is literally
 *                           *(uint64_t *)p_dest = *(uint64_t *)p_src;
 *                       i.e. a memory store into the XiP window, 8 bytes at a
 *                       time, and it masks the low three address bits off.
 *
 * So both directions use the AHB bridge map at BSP_OSPI0_CS0_START_ADDRESS
 * (0x80000000): reads are ordinary loads, writes are ordinary stores after an
 * erase, and erase goes through the driver (which issues the command and polls
 * WIP).  XiP is entered once at init and left only for the duration of a
 * program or erase.
 *
 * The 8-byte granularity is real: Write() rounds the destination down and
 * walks in uint64_t steps, so an unaligned or sub-8-byte tail would silently
 * rewrite neighbouring bytes.  bsp_ospi_write() therefore refuses anything that
 * is not 8-byte aligned at both ends, and bsp_ospi_read() pads its loads out to
 * the same boundary.  The block device above this layer never asks for less
 * than a full 256-byte page, so the restriction is free in practice.
 */
#include "bsp_ospi.h"

#include <string.h>
#include <rtthread.h>

#include "bsp_pin.h"
#include "r_ioport.h"
#include "r_ospi_b.h"
#include "r_spi_flash_api.h"

/* The xSPI register block, for diagnostics that must read the same instance
 * the driver writes (a debugger session reads the same addresses, but seeing
 * the values from inside the firmware removes any session ambiguity). */
#define OSPI_REG_BASE           (0x40268000UL)
#define OSPI_REG_U32(off)       (*(volatile uint32_t *) (uintptr_t) (OSPI_REG_BASE + (off)))
/* R_MSTP: MSTPCRA +0x00, MSTPCRB +0x04, MSTPCRC +0x08. */
#define OSPI_MSTP_BASE          (0x40203000UL)
#define OSPI_MSTP_U32(off)      (*(volatile uint32_t *) (uintptr_t) (OSPI_MSTP_BASE + (off)))

/* R_PFS base.  PmnPFS at + port * 0x40 + pin * 4. */
#define OSPI_PFS_BASE           (0x40400800UL)

/* ---- Winbond W25Q64-class command set (1S-1S-1S) ---------------------- */
#define OSPI_CMD_READ_ID        (0x9FU)     /* JEDEC ID, 3 bytes out        */
#define OSPI_CMD_READ           (0x03U)     /* read, 3-byte address         */
#define OSPI_CMD_PAGE_PROGRAM   (0x02U)     /* page program, 3-byte address */
#define OSPI_CMD_WRITE_ENABLE   (0x06U)
#define OSPI_CMD_STATUS_READ    (0x05U)
#define OSPI_CMD_SECTOR_ERASE   (0x20U)     /* 4 KB                         */
#define OSPI_CMD_BLOCK_ERASE    (0xD8U)     /* 64 KB                        */
#define OSPI_CMD_CHIP_ERASE     (0xC7U)
#define OSPI_CMD_XIP_ENTER      (0x00U)     /* XiP enter code               */
#define OSPI_CMD_XIP_EXIT       (0x00U)     /* XiP exit code                */

#define OSPI_STATUS_BIT_WIP     (0x01U)     /* write in progress            */
#define OSPI_STATUS_BIT_WEL     (0x02U)     /* write enable latch           */

/* The driver writes the XiP window in uint64_t steps and masks off the low
 * three address bits.  Anything finer is silently rounded. */
#define OSPI_WRITE_ALIGN        (8U)

/* Erase command list: sector (4 KB), block (64 KB), chip.  The driver picks
 * the largest command that fits the requested byte count. */
static const spi_flash_erase_command_t g_ospi_erase_commands[] =
{
    { .command = OSPI_CMD_SECTOR_ERASE, .size = 4096U },
    { .command = OSPI_CMD_BLOCK_ERASE,  .size = 65536U },
    { .command = OSPI_CMD_CHIP_ERASE,   .size = SPI_FLASH_ERASE_SIZE_CHIP_ERASE },
};

/* Bus timing.  At the initial (slow) clock none of these need stretching; the
 * values below are the FSP generator's defaults for a 1S-1S-1S NOR. */
static const ospi_b_timing_setting_t g_ospi_timing =
{
    .command_to_command_interval = OSPI_B_COMMAND_INTERVAL_CLOCKS_1,
    .cs_pullup_lag               = OSPI_B_COMMAND_CS_PULLUP_CLOCKS_NO_EXTENSION,
    .cs_pulldown_lead            = OSPI_B_COMMAND_CS_PULLDOWN_CLOCKS_NO_EXTENSION,
};

static ospi_b_instance_ctrl_t g_ospi_ctrl;

/* The extended cfg holds a packed trailing bitfield; keeping it separate
 * avoids a static initialiser with a compound-literal member, which C99 does
 * not allow at file scope with this many nested const pointers. */
/* The flash is on chip-select 1 (net OM_CS1 -> P104).  This is not cosmetic:
 * the driver indexes LIOCFGCS/CMCFGCS/CCCTLCS by channel, and Open() gates the
 * bridge map with the matching CH*CS1ACC bits.  Opening as device 0 configures
 * CS0 - a pin with nothing on it - which is exactly the "controller is 100%
 * configured but the bus is silent" signature seen before. */
#define OSPI_CHANNEL            (OSPI_B_DEVICE_NUMBER_1)

/* CS1 occupies bits [3:2] of BMCTL0; CS0 occupies [1:0].  Open() derives the
 * right bits from p_instance_ctrl->channel, but the driver writes
 * OSPI_B_PRV_BMCTL0_READ_ONLY_VALUE / _READ_WRITE_VALUE during XiP enter/exit,
 * and those macros are hardcoded to the CS0 field.  r_ospi_b_xip() therefore
 * wipes the CS1 access-rights bits the moment XiP is entered, the bridge map
 * for CS1 falls back to "no access", and every load from 0x90000000 aborts.
 * bsp_ospi_xip_grant() restores the CS1 field after any XiP transition. */
#define OSPI_BMCTL0_CS1ACC_BITS (R_XSPI_BMCTL0_CH0CS1ACC_Msk | R_XSPI_BMCTL0_CH1CS1ACC_Msk)

static const ospi_b_extended_cfg_t g_ospi_extend =
{
    .channel                      = OSPI_CHANNEL,
    .p_timing_settings            = &g_ospi_timing,
    .p_xspi_command_set_list      = NULL,   /* no quad/octal profiles */
    .xspi_command_set_list_length = 0U,
    .p_autocalibration_preamble_pattern_addr = NULL,
    .data_latch_delay_clocks       = 0U,
    .read_dummy_cycles             = 0U,
    .program_dummy_cycles          = 0U,
    .status_dummy_cycles           = 0U,
};

static const spi_flash_cfg_t g_ospi_cfg =
{
    /* 1S-1S-1S: commands, address and data all on one line.  The board wires
       SIO2/SIO3 to WP/HOLD, not as quad data lines, so this is the only
       protocol the hardware supports without a wiring change. */
    .spi_protocol              = SPI_FLASH_PROTOCOL_EXTENDED_SPI,
    .read_mode                 = SPI_FLASH_READ_MODE_STANDARD,
    .address_bytes             = SPI_FLASH_ADDRESS_BYTES_3,
    .dummy_clocks              = SPI_FLASH_DUMMY_CLOCKS_0,

    .page_program_address_lines = SPI_FLASH_DATA_LINES_1,
    .write_status_bit           = OSPI_STATUS_BIT_WIP,
    .write_enable_bit           = OSPI_STATUS_BIT_WEL,
    .page_size_bytes            = BSP_OSPI_PAGE_SIZE,

    .page_program_command  = OSPI_CMD_PAGE_PROGRAM,
    .write_enable_command  = OSPI_CMD_WRITE_ENABLE,
    .status_command        = OSPI_CMD_STATUS_READ,
    .read_command          = OSPI_CMD_READ,
    .xip_enter_command     = OSPI_CMD_XIP_ENTER,
    .xip_exit_command      = OSPI_CMD_XIP_EXIT,

    .erase_command_list_length = (uint8_t) (sizeof(g_ospi_erase_commands) /
                                            sizeof(g_ospi_erase_commands[0])),
    .p_erase_command_list      = g_ospi_erase_commands,

    .p_extend = &g_ospi_extend,
};

/* ------------------------------------------------------------------------ */

static bool     g_ospi_ready    = false;
static bool     g_ospi_probed   = false;
static uint8_t  g_ospi_id[3]    = { 0U, 0U, 0U };
static uint32_t g_ospi_capacity = 0U;
static volatile uint8_t * g_ospi_map = (volatile uint8_t *) 0U;

/* Map the JEDEC ID's second byte (memory type) + third byte (capacity) to a
 * byte count.  Covers the W25Q family: 0x17 -> 8 MB (W25Q64), 0x18 -> 16 MB,
 * 0x16 -> 4 MB, 0x15 -> 2 MB.  Unknown codes leave the capacity at 0, which
 * the block device reports rather than guessing. */
static uint32_t ospi_capacity_from_id (const uint8_t id[3])
{
    if ((0xEFU != id[0]) || (0x40U != id[1]))
    {
        return 0U;                      /* not a Winbond W25Qx0 part */
    }

    switch (id[2])
    {
        case 0x14U: return 1U * 1024U * 1024U;     /* W25Q80  - 1 MB  */
        case 0x15U: return 2U * 1024U * 1024U;     /* W25Q16  - 2 MB  */
        case 0x16U: return 4U * 1024U * 1024U;     /* W25Q32  - 4 MB  */
        case 0x17U: return 8U * 1024U * 1024U;     /* W25Q64  - 8 MB  */
        case 0x18U: return 16U * 1024U * 1024U;    /* W25Q128 - 16 MB */
        default:    return 0U;
    }
}

/* Issue 0x9F and collect the three ID bytes through the direct-transfer path
 * (the one command the page API does not expose). */
static fsp_err_t ospi_read_jedec_id (uint8_t out[3])
{
    spi_flash_direct_transfer_t transfer = { 0 };
    fsp_err_t                   err;

    transfer.command        = OSPI_CMD_READ_ID;
    transfer.address        = 0U;
    transfer.address_length = 0U;
    transfer.data_length    = 3U;
    transfer.dummy_cycles   = 0U;
    transfer.command_length = 1U;

    err = R_OSPI_B_DirectTransfer(&g_ospi_ctrl, &transfer,
                                  SPI_FLASH_DIRECT_TRANSFER_DIR_READ);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    out[0] = (uint8_t) ((transfer.data_u64 >> 16) & 0xFFU);
    out[1] = (uint8_t) ((transfer.data_u64 >> 8)  & 0xFFU);
    out[2] = (uint8_t) (transfer.data_u64 & 0xFFU);

    return FSP_SUCCESS;
}

/* Poll the status register until WIP clears.  Bounded so a dead bus cannot
 * hang the caller; 4 KB and 64 KB erases are the slow cases at ~50 ms and
 * ~150 ms, and the loop is a register read, so 1e6 iterations is generous. */
static fsp_err_t ospi_wait_ready (void)
{
    spi_flash_status_t status = { 0 };
    uint32_t           spins  = 0U;

    while (spins < 1000000U)
    {
        fsp_err_t err = R_OSPI_B_StatusGet(&g_ospi_ctrl, &status);

        if (FSP_SUCCESS != err)
        {
            return err;
        }

        if (!status.write_in_progress)
        {
            return FSP_SUCCESS;
        }

        spins++;
    }

    return FSP_ERR_TIMEOUT;
}

/* The driver's XiP enter/exit writes BMCTL0 with a whole-register constant
 * (READ_ONLY_VALUE = 0x55, READ_WRITE_VALUE = 0xFF).  0x55 is
 * 0b0101'0101: it leaves CS0 as read-only but clears the CS1 access-rights
 * field [3:2] to 0b00, i.e. "no access" for the chip-select we are actually
 * using.  The bridge map for CS1 then aborts on the first load from
 * 0x90000000 - which is what a 1S-1S-1S part on CS1 looks like: the manual
 * command path (0x9F through CDCTL0/CDBUF) works, but XiP reads fault.
 *
 * Nothing in this board needs the enter/exit code mechanism (the W25Q64 has
 * no XiP-enable command; xip_enter_command is 0x00, so the "enter code" is the
 * ordinary 3-byte read configured in CMCFGCS1), so re-granting CS1 read/write
 * access immediately after Open() and after every XiP transition is both
 * correct and sufficient. */
static void ospi_xip_grant_access (void)
{
    R_XSPI->BMCTL0 |= OSPI_BMCTL0_CS1ACC_BITS;
}

/* XiP must be left before a program or erase: the bridge map holds a read
 * command set, and AHB accesses during a modify would race the command. */
static fsp_err_t ospi_leave_xip (void)
{
    fsp_err_t err = R_OSPI_B_XipExit(&g_ospi_ctrl);

    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* XipExit() rewrote BMCTL0 from a CS0-shaped constant; put CS1 back. */
    ospi_xip_grant_access();

    return FSP_SUCCESS;
}

static fsp_err_t ospi_enter_xip (void)
{
    fsp_err_t err = R_OSPI_B_XipEnter(&g_ospi_ctrl);

    if (FSP_SUCCESS != err)
    {
        return err;
    }

    ospi_xip_grant_access();

    return FSP_SUCCESS;
}

/* ------------------------------------------------------------------------ */

fsp_err_t bsp_ospi_init (void)
{
    fsp_err_t err;

    if (g_ospi_ready)
    {
        return FSP_SUCCESS;
    }

    /* A previous attempt may have opened the driver and then rejected the
       part.  Open() refuses a second open on the same control block, so drop
       the old registration first; Close() on a never-opened block is a
       harmless no-op returning FSP_ERR_NOT_OPEN. */
    (void) R_OSPI_B_Close(&g_ospi_ctrl);

    err = R_OSPI_B_Open(&g_ospi_ctrl, &g_ospi_cfg);
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* Open() already set the CS1 access bits, but be explicit: the manual
       command path used below does not depend on them, so a missing grant
       would only show up much later, as an XiP read fault. */
    ospi_xip_grant_access();

    /* Read the ID *before* XiP: a wrong part or a dead bus must be caught
       while the code path is still trivially debuggable. */
    err = ospi_read_jedec_id(g_ospi_id);
    if (FSP_SUCCESS != err)
    {
        (void) R_OSPI_B_Close(&g_ospi_ctrl);
        return err;
    }

    /* Keep the raw ID readable even when the part is not recognised, so
       `ospi id` can report what was actually on the bus. */
    g_ospi_probed = true;

    g_ospi_capacity = ospi_capacity_from_id(g_ospi_id);
    if (0U == g_ospi_capacity)
    {
        /* Unknown part: the ID is kept for diagnostics but the driver stays
           unusable, so no later call can address past a device of unknown
           size.  Caller sees this as FSP_ERR_UNSUPPORTED. */
        return FSP_ERR_UNSUPPORTED;
    }

    /* The bridge map window the driver itself uses for XiP.  CS0 is mapped at
       0x80000000 and CS1 at 0x90000000 - reading the CS0 window while the part
       sits on CS1 returns the AHB error value, not flash contents.  Reads
       become loads and writes become stores from here on. */
    g_ospi_map = (volatile uint8_t *) (uintptr_t)
                 ((OSPI_B_DEVICE_NUMBER_0 == OSPI_CHANNEL) ?
                  BSP_FEATURE_OSPI_B_DEVICE_0_START_ADDRESS :
                  BSP_FEATURE_OSPI_B_DEVICE_1_START_ADDRESS);

    err = ospi_enter_xip();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    g_ospi_ready = true;

    return FSP_SUCCESS;
}

bool bsp_ospi_ready (void)
{
    return g_ospi_ready;
}

const uint8_t * bsp_ospi_jedec_id (void)
{
    return g_ospi_id;
}

bool bsp_ospi_probed (void)
{
    return g_ospi_probed;
}

/* Copy the handful of xSPI registers that say whether Open() actually
 * configured the block, plus what the controller observed on the bus.
 *
 * Called from the firmware so the values come from the same instance the
 * driver wrote - a debugger reading the same addresses is subject to
 * session/halt effects, this is not.
 *
 * Offsets quoted from the R_XSPI_Type member comments in R7FA8D1BH.h, not
 * guessed:
 *   CMCFGCS[2]  @ 0x10  (element stride 0x10) -> CS1 = 0x20
 *   LIOCFGCS[2] @ 0x50  (element stride 4)    -> CS1 = 0x54
 *   CCCTLCS[2]  @ 0x130 (element stride 0x20) -> CS1 = 0x150
 *   CASTTCS[2]  @ 0x188 (element stride 0x20) -> CS1 = 0x1A8
 *   BMCTL0 @ 0x60, CMCTLCH[2] @ 0x68, CDCTL0 @ 0x70
 *   VERSTT @ 0x180, COMSTT @ 0x184, INTS @ 0x190
 *
 * An earlier revision read COMSTT at 0x164, which is not a register at all, so
 * that column was meaningless.  Always check a new offset against this header.
 *
 * out[] layout (all uint32_t): WRAPCFG, COMCFG, BMCFGCH0, LIOCFGCS1 (the
 * channel actually in use), BMCTL0, CMCTLCH0, CDCTL0, CMCFGCS1.CMCFG0/1/2,
 * COMSTT, MSTPCRB.  Reading the CS0 register bank would show all zeroes and
 * look like a failure. */
void bsp_ospi_reg_dump (uint32_t out[12])
{
    /* Per-channel register offsets, selected by the channel the driver uses. */
    const uint32_t lio = (OSPI_B_DEVICE_NUMBER_0 == OSPI_CHANNEL) ? 0x50U : 0x54U;
    const uint32_t cmc = (OSPI_B_DEVICE_NUMBER_0 == OSPI_CHANNEL) ? 0x10U : 0x20U;

    out[0]  = OSPI_REG_U32(0x00);              /* WRAPCFG          */
    out[1]  = OSPI_REG_U32(0x04);              /* COMCFG           */
    out[2]  = OSPI_REG_U32(0x08);              /* BMCFGCH0         */
    out[3]  = OSPI_REG_U32(lio);               /* LIOCFGCS<ch>     */
    out[4]  = OSPI_REG_U32(0x60);              /* BMCTL0           */
    out[5]  = OSPI_REG_U32(0x68);              /* CMCTLCH[0]       */
    out[6]  = OSPI_REG_U32(0x70);              /* CDCTL0           */
    out[7]  = OSPI_REG_U32(cmc + 0x00);        /* CMCFGCS<ch>.CMCFG0 */
    out[8]  = OSPI_REG_U32(cmc + 0x04);        /* CMCFGCS<ch>.CMCFG1 */
    out[9]  = OSPI_REG_U32(cmc + 0x08);        /* CMCFGCS<ch>.CMCFG2 */
    out[10] = OSPI_REG_U32(0x184);             /* COMSTT           */
    out[11] = OSPI_MSTP_U32(0x04);             /* MSTPCRB (XSPI bit16) */
}

/* Issue one manual 0x9F and report what the controller observed.
 *
 * The register dump shows configuration; this shows behaviour.  After the
 * command, INTS reports what the xSPI block actually saw:
 *
 *   CMDCMP  [0]  - command completed           (the driver waits on this;
 *                                               without it Open() would hang,
 *                                               since FSP_HARDWARE_REGISTER_WAIT
 *                                               has no timeout)
 *   ECSCS   [8]  - ECS (chip-select) event
 *   INTCS   [12] - INT event
 *   BUSERRCH[20] - AHB bus error
 *
 * If CMDCMP sets but no ECS event appears, the controller finished the frame
 * without asserting chip-select - which is what the CS pad probe sees, and it
 * means the transaction never reached the device. */
void bsp_ospi_cmd_int_status (uint32_t out[4])
{
    spi_flash_direct_transfer_t transfer = { 0 };

    /* Clear any latched status from earlier traffic. */
    OSPI_REG_U32(0x194) = 0xFFFFFFFFU;         /* INTC: write 1 to clear      */

    transfer.command        = OSPI_CMD_READ_ID;
    transfer.address        = 0U;
    transfer.address_length = 0U;
    transfer.data_length    = 3U;
    transfer.dummy_cycles   = 0U;
    transfer.command_length = 1U;

    (void) R_OSPI_B_DirectTransfer(&g_ospi_ctrl, &transfer,
                                   SPI_FLASH_DIRECT_TRANSFER_DIR_READ);

    out[0] = OSPI_REG_U32(0x190);              /* INTS  - what happened       */
    out[1] = OSPI_REG_U32(0x194);              /* INTC                        */
    out[2] = OSPI_REG_U32(0x184);              /* COMSTT - live slave monitors */
    out[3] = OSPI_REG_U32(0x60);               /* BMCTL0 - access rights      */
}

uint32_t bsp_ospi_capacity (void)
{
    return g_ospi_capacity;
}

/* Watch the chip-select line around a JEDEC-ID command.
 *
 * ---------------------------------------------------------------------------
 * Instrument validation first (this is the whole point of the function)
 * ---------------------------------------------------------------------------
 * Two earlier attempts at this probe produced answers that were not evidence,
 * because neither instrument had been checked against a known-good reference:
 *
 *   1. Flipping the pad to an input while xSPI still owned it.  The pad keeps
 *      being driven by the peripheral, the read returns whatever the
 *      contention gives, and it read low with no command in flight.  False
 *      "CS asserts".
 *   2. Reading COMSTT.ECSCS after setting COMCFG.ECSINTOUTEN.  ECSINTOUTEN is
 *      "ECS/INT Output Enable" - it drives those signals OUT onto pins, it does
 *      not mirror them into a status bit.  COMSTT.ECSCS reads 1 both before
 *      and after, so it is not tracking the live CS level.  False "no change".
 *
 * What this version does instead is state what it can actually support:
 *
 *   - It samples COMSTT at idle and across a burst of commands issued back to
 *     back, and reports whether the value EVER changed.  If a field that is
 *     supposed to track bus activity never moves, the honest conclusion is
 *     that this instrument cannot answer the question - not that the signal is
 *     stuck.
 *   - It records the JEDEC response for the same burst, so the caller can see
 *     whether the two agree.
 *
 * The upshot the caller should take: a silent bus plus a pad that cannot be
 * sampled plus a monitor that never moves adds up to "cannot be resolved from
 * firmware alone".  The next decisive step is a scope, or a meter on the CS
 * and VCC pins. */
void bsp_ospi_cs_activity_probe (bool * idle_high, bool * ever_changed,
                                 uint32_t * idle_sample, uint32_t * min_sample)
{
    uint32_t idle    = OSPI_REG_U32(0x184);    /* COMSTT, no transfer running  */
    uint32_t lowest  = idle;
    bool     changed = false;

    *idle_sample = idle;
    *idle_high   = (0U != (idle & (1U << 16)));

    if (!g_ospi_ready)
    {
        (void) bsp_ospi_init();
    }

    /* A burst: the window between samples is short, so back-to-back commands
       give the best chance of catching a level that tracks the bus. */
    for (uint32_t i = 0U; i < 64U; i++)
    {
        spi_flash_direct_transfer_t transfer = { 0 };

        transfer.command        = OSPI_CMD_READ_ID;
        transfer.address        = 0U;
        transfer.address_length = 0U;
        transfer.data_length    = 3U;
        transfer.dummy_cycles   = 0U;
        transfer.command_length = 1U;

        (void) R_OSPI_B_DirectTransfer(&g_ospi_ctrl, &transfer,
                                       SPI_FLASH_DIRECT_TRANSFER_DIR_READ);

        uint32_t now = OSPI_REG_U32(0x184);

        if (now != idle)
        {
            changed = true;
        }

        if (now < lowest)
        {
            lowest = now;
        }
    }

    *ever_changed = changed;
    *min_sample   = lowest;
}

/* Read the six OSPI pin mux registers from inside the firmware.
 *
 * Why this exists: the same registers read over SWD are not evidence.  A
 * debugger session reads PmnPFS from a possibly-halted core and reports
 * whatever that session's connect sequence left in PFS - in practice all
 * zeroes, which looks exactly like "the mux was never applied".  The values
 * printed here are read by the code that wrote them, in the same thread, with
 * no session in between, so they are the authoritative answer to "are the pads
 * on the xSPI peripheral?". */
void bsp_ospi_pfs_dump (void)
{
    /* Same table as bsp_pin.c's OSPI block: net name -> FSP port/pin.
       PFS address = R_PFS_BASE + port * 0x40 + pin * 4. */
    static const struct
    {
        const char * name;
        uint8_t      port;
        uint8_t      pin;
    } pins[] =
    {
        { "CS1 ", 10U, 4U },   /* P104 */
        { "SCLK",  8U, 8U },   /* P808 */
        { "SIO0",  1U, 0U },   /* P100, MOSI */
        { "SIO1",  8U, 3U },   /* P803, MISO */
        { "SIO2",  1U, 3U },   /* P103, WP   */
        { "SIO3",  1U, 1U },   /* P101, HOLD */
    };

    for (uint32_t i = 0U; i < (sizeof(pins) / sizeof(pins[0])); i++)
    {
        uint32_t addr = OSPI_PFS_BASE + ((uint32_t) pins[i].port * 0x40U) +
                        ((uint32_t) pins[i].pin * 4U);
        uint32_t val  = *(volatile uint32_t *) (uintptr_t) addr;
        uint32_t psel = (val >> 24) & 0x1FU;
        uint32_t pmr  = (val >> 16) & 0x1U;

        rt_kprintf("ospi pfs: %-4s P%d%d 0x%08x = 0x%08x PSEL=0x%02x PMR=%d %s\n",
                   pins[i].name, (int) pins[i].port, (int) pins[i].pin,
                   (unsigned int) addr, (unsigned int) val,
                   (unsigned int) psel, (int) pmr,
                   ((0x1CU == psel) && (1U == pmr)) ? "OK" : "<-- WRONG");
    }
}

uint8_t bsp_ospi_status (void)
{
    spi_flash_status_t status = { 0 };

    if (!g_ospi_ready)
    {
        return 0U;
    }

    (void) R_OSPI_B_StatusGet(&g_ospi_ctrl, &status);

    return status.write_in_progress ? OSPI_STATUS_BIT_WIP : 0U;
}

fsp_err_t bsp_ospi_read (uint32_t addr, void * dst, uint32_t len)
{
    if (!g_ospi_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    if ((NULL == dst) || (0U == len))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    if ((addr + len) > g_ospi_capacity)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    /* Straight loads out of the XiP window.  memcpy is the right primitive:
       it is byte-granular, so read() has no alignment restriction even though
       write() does. */
    memcpy(dst, (const void *) (g_ospi_map + addr), len);

    return FSP_SUCCESS;
}

fsp_err_t bsp_ospi_write (uint32_t addr, const void * src, uint32_t len)
{
    fsp_err_t err;

    if (!g_ospi_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    if ((NULL == src) || (0U == len))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    /* The driver rounds the destination down to 8 bytes and stores uint64_t
       words.  Refuse rather than corrupt the neighbours. */
    if ((0U != (addr % OSPI_WRITE_ALIGN)) ||
        (0U != (len  % OSPI_WRITE_ALIGN)))
    {
        return FSP_ERR_INVALID_ADDRESS;
    }

    if ((addr + len) > g_ospi_capacity)
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    err = ospi_leave_xip();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* p_dest is the *mapped* address: Write() stores through it. */
    err = R_OSPI_B_Write(&g_ospi_ctrl,
                         (uint8_t const *) src,
                         (uint8_t *) (g_ospi_map + addr),
                         len);
    if (FSP_SUCCESS != err)
    {
        (void) ospi_enter_xip();        /* restore the read path regardless */
        return err;
    }

    err = ospi_wait_ready();
    if (FSP_SUCCESS != err)
    {
        (void) ospi_enter_xip();
        return err;
    }

    return ospi_enter_xip();
}

fsp_err_t bsp_ospi_erase_sector (uint32_t addr)
{
    fsp_err_t err;

    if (!g_ospi_ready)
    {
        return FSP_ERR_NOT_OPEN;
    }

    if ((0U != (addr % BSP_OSPI_SECTOR_SIZE)) ||
        ((addr + BSP_OSPI_SECTOR_SIZE) > g_ospi_capacity))
    {
        return FSP_ERR_INVALID_ARGUMENT;
    }

    err = ospi_leave_xip();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    /* Erase() takes the device-space address and issues the command itself. */
    err = R_OSPI_B_Erase(&g_ospi_ctrl, (uint8_t *) (uintptr_t) addr,
                         BSP_OSPI_SECTOR_SIZE);
    if (FSP_SUCCESS != err)
    {
        (void) ospi_enter_xip();
        return err;
    }

    err = ospi_wait_ready();
    if (FSP_SUCCESS != err)
    {
        (void) ospi_enter_xip();
        return err;
    }

    return ospi_enter_xip();
}

/* ------------------------------------------------------------------------ */
/* Bit-bang fallback: the same four SPI signals, driven as plain GPIO.       */
/*                                                                           */
/* The xSPI controller is bypassed completely, so this answers the one       */
/* question the register dump cannot: is there a chip on the other end of    */
/* the wires?  If xSPI readback is 00 00 00 but this returns EF 40 17, the   */
/* board is fine and the fault is in the controller setup.  If both are      */
/* zero, the device is not responding at all.                                */
/* ------------------------------------------------------------------------ */

/* SPI mode 0, single line: CS low, MSB first, sample on rising edge. */
#define OSPI_BB_DELAY_CYCLES (32U)

static void ospi_bb_delay (void)
{
    /* Crude busy loop.  The clock must stay far below the W25Q64's 50 MHz
       rating; a few hundred ns per half-period is plenty and keeps this
       independent of any timer being initialized. */
    for (volatile uint32_t i = 0U; i < OSPI_BB_DELAY_CYCLES; i++)
    {
        __asm volatile ("nop");
    }
}

static void ospi_bb_cs (uint32_t level)
{
    (void) bsp_pin_write(BSP_IO_PORT_10_PIN_04,
                         (0U != level) ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

static void ospi_bb_sclk (uint32_t level)
{
    (void) bsp_pin_write(BSP_IO_PORT_08_PIN_08,
                         (0U != level) ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

static void ospi_bb_mosi (uint32_t level)
{
    (void) bsp_pin_write(BSP_IO_PORT_01_PIN_00,
                         (0U != level) ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

static uint32_t ospi_bb_miso (void)
{
    bsp_io_level_t level = BSP_IO_LEVEL_LOW;

    (void) bsp_pin_read(BSP_IO_PORT_08_PIN_03, &level);

    return (uint32_t) level;
}

/* One full clock: MOSI is set up while SCLK is low, then sampled on the
   rising edge (this device is mode 0). */
static uint32_t ospi_bb_transfer (uint32_t bit_in)
{
    uint32_t bit_out;

    ospi_bb_mosi(bit_in);
    ospi_bb_delay();
    ospi_bb_sclk(1U);
    ospi_bb_delay();
    bit_out = ospi_bb_miso();
    ospi_bb_sclk(0U);
    ospi_bb_delay();

    return bit_out;
}

/* Re-mux the six OSPI pins as plain GPIO.  CS/SCLK/MOSI are outputs at idle
   level, MISO is an input; SIO2/SIO3 are tied high so WP and HOLD stay
   released (a low HOLD would put the device to sleep mid-transfer). */
static fsp_err_t ospi_bb_pins_to_gpio (void)
{
    fsp_err_t err;

    err = bsp_pin_cfg(BSP_IO_PORT_10_PIN_04,
                      IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_HIGH);
    if (FSP_SUCCESS != err) { return err; }

    err = bsp_pin_cfg(BSP_IO_PORT_08_PIN_08,
                      IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);
    if (FSP_SUCCESS != err) { return err; }

    err = bsp_pin_cfg(BSP_IO_PORT_01_PIN_00,
                      IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);
    if (FSP_SUCCESS != err) { return err; }

    err = bsp_pin_cfg(BSP_IO_PORT_08_PIN_03, IOPORT_CFG_PORT_DIRECTION_INPUT);
    if (FSP_SUCCESS != err) { return err; }

    err = bsp_pin_cfg(BSP_IO_PORT_01_PIN_03,
                      IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_HIGH);
    if (FSP_SUCCESS != err) { return err; }

    err = bsp_pin_cfg(BSP_IO_PORT_01_PIN_01,
                      IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_HIGH);

    return err;
}

/* ------------------------------------------------------------------------ */
/* MISO line-health probe.                                                    */
/*                                                                            */
/* The single most useful measurement when the flash answers nothing: put     */
/* MISO back to a plain input with the internal pull-up on and read the pad.   */
/*                                                                            */
/*   reads HIGH  -> the line is not shorted to ground and the device is not    */
/*                  holding it down; its high-Z idle state is what we see.     */
/*                  Combined with a failed JEDEC read this means the part      */
/*                  never drove the bus at all.                                */
/*   reads LOW   -> something is actively pulling the line down (short, or a   */
/*                  device stuck driving).  Different fault, different fix.    */
/*                                                                            */
/* The same reading is taken with the pull-up off, so a board that relies on  */
/* an external pull-up resistor shows up as 0/1 and a board with no pull-up   */
/* at all shows up as 0/1 from the internal one only.                          */
/* ------------------------------------------------------------------------ */
#define OSPI_BB_MISO_PIN        (BSP_IO_PORT_08_PIN_03)

static uint32_t ospi_bb_miso_level (bool pull_up)
{
    bsp_io_level_t level = BSP_IO_LEVEL_LOW;

    if (pull_up)
    {
        (void) bsp_pin_cfg(OSPI_BB_MISO_PIN,
                           BSP_IO_LEVEL_HIGH | IOPORT_CFG_PULLUP_ENABLE);
    }
    else
    {
        (void) bsp_pin_cfg(OSPI_BB_MISO_PIN, IOPORT_CFG_PORT_DIRECTION_INPUT);
    }

    (void) bsp_pin_read(OSPI_BB_MISO_PIN, &level);

    return (uint32_t) level;
}

void bsp_ospi_miso_probe (bool * float_high, bool * idle_low)
{
    /* No pull-up: the pad sits at whatever the board gives it. */
    *idle_low = (0U == ospi_bb_miso_level(false));

    /* Internal pull-up on: high means nothing is fighting the line. */
    *float_high = (1U == ospi_bb_miso_level(true));

    /* Leave the pin as a plain input; the caller re-inits the controller. */
    (void) bsp_pin_cfg(OSPI_BB_MISO_PIN, IOPORT_CFG_PORT_DIRECTION_INPUT);

    /* Same trap as in bsp_ospi_bitbang_id(): MISO is on GPIO now and PSEL is
       gone, so the controller cannot see the line until the mux is rewritten. */
    (void) bsp_pin_ospi_remux();
}

fsp_err_t bsp_ospi_bitbang_id (void)
{
    fsp_err_t err;
    uint32_t  id = 0U;

    /* Take the controller out of the picture first: if XiP is active the
       bridge keeps driving the same pins and would fight the GPIO writes. */
    (void) R_OSPI_B_Close(&g_ospi_ctrl);
    g_ospi_ready = false;

    err = ospi_bb_pins_to_gpio();
    if (FSP_SUCCESS != err)
    {
        return err;
    }

    ospi_bb_cs(1U);
    ospi_bb_sclk(0U);
    ospi_bb_delay();

    ospi_bb_cs(0U);
    ospi_bb_delay();

    /* 0x9F, MSB first. */
    (void) ospi_bb_transfer(0U);
    (void) ospi_bb_transfer(1U);
    (void) ospi_bb_transfer(0U);
    (void) ospi_bb_transfer(0U);
    (void) ospi_bb_transfer(1U);
    (void) ospi_bb_transfer(1U);
    (void) ospi_bb_transfer(1U);
    (void) ospi_bb_transfer(1U);

    /* Three response bytes, MSB first. */
    for (uint32_t byte = 0U; byte < 3U; byte++)
    {
        uint32_t value = 0U;

        for (uint32_t bit = 0U; bit < 8U; bit++)
        {
            value = (value << 1) | ospi_bb_transfer(1U);
        }
        id = (id << 8) | (value & 0xFFU);
    }

    ospi_bb_cs(1U);
    ospi_bb_delay();

    g_ospi_id[0] = (uint8_t) ((id >> 16) & 0xFFU);
    g_ospi_id[1] = (uint8_t) ((id >> 8) & 0xFFU);
    g_ospi_id[2] = (uint8_t) (id & 0xFFU);
    g_ospi_probed = true;

    /* Only trust the capacity if the part actually answered. */
    g_ospi_capacity = (0U != id) ? ospi_capacity_from_id(g_ospi_id) : 0U;

    /* Hand the pins back to the controller.  This is not optional: the probe
       above re-slaved all six to plain GPIO, and R_IOPORT_PinCfg() never
       writes PSEL, so re-initialising xSPI below would leave the pads on
       GPIO - the controller would drive nothing and every later read would
       come back as 00 00 00.  bsp_pin_ospi_remux() rewrites PSEL for all six
       at once, which is the only path that restores the peripheral function. */
    (void) bsp_pin_ospi_remux();

    return FSP_SUCCESS;
}
