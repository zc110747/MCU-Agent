/* ---------------------------------------------------------------------------
 * TinyUSB Mass Storage Class callbacks - the SD card is exposed to the USB
 * host as a single LUN (logical unit). All block moves go through bsp_sdcard.c,
 * exactly as FatFs does, so the two views of the card can never disagree.
 *
 * Two rules keep the host from spending seconds in error recovery:
 *   1. never return 0 from read10/write10 - the MSC stack reads 0 as
 *      "retry later" and spins on the same command forever;
 *   2. honour the (lba, offset) pair by bouncing through one sector when a
 *      command is not sector aligned, instead of silently rounding.
 * -------------------------------------------------------------------------*/

#include "tusb.h"
#include "bsp_sdcard.h"
#include "sd_app.h"   /* fatfs_release_for_host / fatfs_reacquire */

#include <string.h>

/* Bounce sector for unaligned / partial-sector transfers. */
static uint8_t g_msc_sector[512];

/* Successful reads are the bulk of the traffic and would flush the trace ring
* within a second, so only a bounded number of them are recorded (failures
* always are). The budget is re-armed on every SET_CONFIGURATION so that a
* trace read right after a plug-in contains the whole mount sequence. A cold
* mount is MBR + BPB + FAT + directory, well under 90 reads. */
#define MSC_TRACE_READ_BUDGET 80
static uint16_t g_msc_rd_trace = MSC_TRACE_READ_BUDGET;

/* Re-arm read tracing for the next host mount (called from tud_mount_cb). */
void msc_trace_arm(void)
{
    g_msc_rd_trace = MSC_TRACE_READ_BUDGET;
}

static void put_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t) v;
}

void tud_msc_inquiry_cb(uint8_t lun,
                        uint8_t vendor_id[8],
                        uint8_t product_id[16],
                        uint8_t product_rev[4])
{
    (void) lun;
    /* The stack has already flagged this as a REMOVABLE device (U-disk). */
    static const char v[] = "STM32H7";
    static const char p[] = "SD_Card";
    /* Revision is part of the USBSTOR device instance path. Bumping it makes
     * Windows create a fresh devnode instead of reusing a stale one that was
     * left "present but never started" by an earlier failed enumeration. */
    static const char r[] = "1.1";
    memcpy(vendor_id,   v, sizeof(v) - 1);
    memcpy(product_id,  p, sizeof(p) - 1);
    memcpy(product_rev, r, sizeof(r) - 1);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void) lun;
    bool const ready = sdcard_present();
    sd_trace_add(SD_TR_TUR, ready ? 1U : 0U, 0, 0);
    return ready;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
{
    (void) lun;
    *block_count = sdcard_block_count();
    *block_size  = (uint16_t) sdcard_block_size();
    sd_trace_add(SD_TR_CAP, 0, *block_size, *block_count);
}

int32_t tud_msc_read10_cb(uint8_t lun,
                          uint32_t lba,
                          uint32_t offset,
                          void* buffer,
                          uint32_t bufsize)
{
    (void) lun;
    uint32_t const blk = sdcard_block_size();

    if (blk == 0 || bufsize == 0) return -1;   /* 0 would mean "busy" */

    bool const keep = (g_msc_rd_trace > 0U);
    if (keep)
    {
        g_msc_rd_trace--;
        sd_trace_add(SD_TR_RD, 0, (uint16_t) bufsize, lba);
    }

    /* Fast path: sector aligned, whole sectors. */
    if (offset == 0U && (bufsize % blk) == 0U)
    {
        if (sdcard_read_blocks((uint8_t*) buffer, lba, bufsize / blk) != SD_ST_OK)
        {
            return -1;
        }
        return (int32_t) bufsize;
    }

    /* Slow path: bounce one sector at a time. */
    uint32_t sector = lba + (offset / blk);
    uint32_t const skip = offset % blk;
    uint32_t       pos  = 0;

    while (pos < bufsize)
    {
        uint32_t const inblk = (skip + pos) % blk;
        uint32_t       n     = blk - inblk;
        if (n > (bufsize - pos)) n = bufsize - pos;

        if (sdcard_read_blocks(g_msc_sector, sector, 1) != SD_ST_OK)
        {
            return -1;
        }
        memcpy((uint8_t*) buffer + pos, g_msc_sector + inblk, n);
        pos += n;
        sector++;
    }
    return (int32_t) bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun,
                           uint32_t lba,
                           uint32_t offset,
                           uint8_t* buffer,
                           uint32_t bufsize)
{
    (void) lun;
    uint32_t const blk = sdcard_block_size();

    if (blk == 0 || bufsize == 0) return -1;   /* 0 would mean "busy" */

    sd_trace_add(SD_TR_WR, 0, (uint16_t) bufsize, lba);

    if (offset == 0U && (bufsize % blk) == 0U)
    {
        if (sdcard_write_blocks(buffer, lba, bufsize / blk) != SD_ST_OK)
        {
            return -1;
        }
        sd_trace_add(SD_TR_WROK, 0, (uint16_t) bufsize, lba);
        return (int32_t) bufsize;
    }

    /* Slow path: read-modify-write for the first and last partial sector. */
    uint32_t sector = lba + (offset / blk);
    uint32_t const skip = offset % blk;
    uint32_t       pos  = 0;

    while (pos < bufsize)
    {
        uint32_t const inblk = (skip + pos) % blk;
        uint32_t       n     = blk - inblk;
        if (n > (bufsize - pos)) n = bufsize - pos;

        if (inblk == 0U && n == blk)
        {
            if (sdcard_write_blocks(buffer + pos, sector, 1) != SD_ST_OK)
            {
                return -1;
            }
        }
        else
        {
            if (sdcard_read_blocks(g_msc_sector, sector, 1) != SD_ST_OK)
            {
                return -1;
            }
            memcpy(g_msc_sector + inblk, buffer + pos, n);
            if (sdcard_write_blocks(g_msc_sector, sector, 1) != SD_ST_OK)
            {
                return -1;
            }
        }
        pos += n;
        sector++;
    }
    sd_trace_add(SD_TR_WROK, 1, (uint16_t) bufsize, lba);
    return (int32_t) bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun,
                        uint8_t const scsi_cmd[16],
                        void* buffer,
                        uint16_t bufsize)
{
    (void) lun;
    uint8_t const op = scsi_cmd[0];
    uint8_t*      out = (uint8_t*) buffer;

    sd_trace_add(SD_TR_SCSI, op, bufsize, 0);

    switch (op)
    {
        /* Commands the stack does not implement itself. Answering them is
         * cheaper than a STALL: every STALLed command makes the host run a
         * sense/recovery cycle before it carries on with the mount. */

        case 0x35:  /* SYNCHRONIZE CACHE(10) - routine on Linux/macOS   */
        case 0x2F:  /* VERIFY(10)            - no data stage            */
        case 0x1D:  /* SEND DIAGNOSTIC       - no data stage            */
        case 0x1B:  /* START STOP UNIT       - handled by the stack too */
        case 0x1E:  /* PREVENT ALLOW MEDIUM REMOVAL                     */
        case 0x00:  /* TEST UNIT READY (already handled by the stack)   */
            return 0;

        case 0x9E:  /* READ CAPACITY(16)                                */
        {
            uint32_t const blocks = sdcard_block_count();
            uint32_t const bsize  = sdcard_block_size();

            if (out == NULL || bufsize < 32U || blocks == 0U || bsize == 0U)
            {
                return -1;
            }
            memset(out, 0, 32);
            put_be32(&out[4], blocks - 1U);   /* last logical block */
            put_be32(&out[8], bsize);
            return 32;
        }

        default:
            sd_trace_add(SD_TR_SCSIUNK, op, bufsize, 0);
            return -1;   /* genuinely unsupported -> STALL */
    }
}

/* The host tells us when it mounts (load) and ejects (unload) the volume.
 * While the host owns the card we release the device-side FatFs mount so the
 * two writers can never race and corrupt the filesystem. */
bool tud_msc_start_stop_cb(uint8_t lun,
                           uint8_t power_condition,
                           bool start,
                           bool load_eject)
{
    (void) lun; (void) power_condition;

    sd_trace_add(SD_TR_SSU, start ? 1U : 0U, load_eject ? 1U : 0U, 0);

    if (load_eject)
    {
        if (start) fatfs_release_for_host();
        else       fatfs_reacquire();
    }
    return true;
}
