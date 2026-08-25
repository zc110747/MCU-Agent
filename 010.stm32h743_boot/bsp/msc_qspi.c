/* ---------------------------------------------------------------------------
 * TinyUSB Mass Storage Class callbacks - exposes the 8 MB QSPI flash to the USB
 * host as a single LUN (removable disk). All block moves go through the QSPI
 * driver in HAL indirect mode (not XIP), as required.
 *
 * The flash is a raw device: the host formats it (FAT/exFAT) on first use.
 * Block size is 512 B; the flash native erase unit is 4 KB and program page is
 * 256 B, so writes are implemented as read-modify-erase-program at sector
 * granularity to keep partial writes safe.
 * -------------------------------------------------------------------------*/

#include "tusb.h"
#include "qspi.h"
#include "uart.h"

/* Logical block size and count. */
#define BLOCK_SIZE   512U
#define SECTOR_SIZE  QSPI_SECTOR_SIZE   /* 4096 bytes physical erase unit */

/* Inquiry strings - the stack already marks the device as removable. */
void tud_msc_inquiry_cb(uint8_t lun,
                        uint8_t vendor_id[8],
                        uint8_t product_id[16],
                        uint8_t product_rev[4]) {
    (void) lun;
    static const char v[] = "STM32H7";
    static const char p[] = "QSPI_DISK";
    static const char r[] = "1.0";
    memcpy(vendor_id,   v, sizeof(v) - 1);
    memcpy(product_id,  p, sizeof(p) - 1);
    memcpy(product_rev, r, sizeof(r) - 1);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void) lun;
    /* The QSPI is always present; report ready. */
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
    (void) lun;
    *block_count = (uint32_t)(QSPI_FLASH_SIZE / BLOCK_SIZE);
    *block_size  = (uint16_t) BLOCK_SIZE;
}

/* Read one or more 512-byte logical blocks starting at lba + offset (offset is
 * always 0 here because CFG_TUD_MSC_EP_BUFSIZE == BLOCK_SIZE). */
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void* buffer, uint32_t bufsize) {
    (void) lun; (void) offset;
    uint32_t addr = lba * BLOCK_SIZE;
    if (BSP_QSPI_ReadIndirect(addr, (uint8_t*) buffer, bufsize) != QSPI_OK)
        return -1;
    return (int32_t) bufsize;
}

/* --- 4 KB write cache -----------------------------------------------------
 * The flash cannot be written in place (must erase before program), so we keep a
 * single 4 KB sector cache. Consecutive 512-byte writes that fall inside the
 * same physical sector are merged in RAM; the sector is only erased/programmed
 * when (a) the incoming write targets a different sector, or (b) the host
 * unmounts / stops the device. This collapses many small writes into one erase. */
static uint8_t  g_cache[QSPI_SECTOR_SIZE];
static uint32_t g_cache_sector = 0xFFFFFFFFU;   /* invalid until first load */
static bool     g_cache_dirty  = false;

static int flush_cache(void)
{
    if (!g_cache_dirty)
        return 0;
    if (BSP_QSPI_EraseSector(g_cache_sector) != QSPI_OK)
        return -1;
    for (uint32_t pg = 0; pg < QSPI_SECTOR_SIZE; pg += QSPI_PAGE_SIZE) {
        if (BSP_QSPI_WritePage(g_cache_sector + pg, &g_cache[pg], QSPI_PAGE_SIZE) != QSPI_OK)
            return -1;
    }
    g_cache_dirty = false;
    return 0;
}

/* Write one or more 512-byte logical blocks through the sector cache. */
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t* buffer, uint32_t bufsize) {
    (void) lun; (void) offset;

    uint32_t pos       = lba * BLOCK_SIZE;
    uint32_t remaining = bufsize;
    uint8_t* src       = buffer;

    while (remaining > 0) {
        uint32_t sec_addr   = pos & ~(SECTOR_SIZE - 1);
        uint32_t in_sec_off = pos - sec_addr;
        uint32_t copy       = remaining;
        if (in_sec_off + copy > SECTOR_SIZE)
            copy = SECTOR_SIZE - in_sec_off;

        /* Switching to a new physical sector: flush the previous one first. */
        if (sec_addr != g_cache_sector) {
            if (flush_cache() != 0)
                return -1;
            if (BSP_QSPI_ReadIndirect(sec_addr, g_cache, SECTOR_SIZE) != QSPI_OK)
                return -1;
            g_cache_sector = sec_addr;
        }

        memcpy(&g_cache[in_sec_off], src, copy);
        g_cache_dirty = true;

        pos       += copy;
        src       += copy;
        remaining -= copy;
    }

    return (int32_t) bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16],
                        void* buffer, uint16_t bufsize) {
    (void) lun; (void) buffer; (void) bufsize;
    /* SYNCHRONIZE CACHE (0x35) is routinely sent by hosts; flush on it too. */
    if (scsi_cmd[0] == 0x35)
        return flush_cache();
    return -1;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition,
                           bool start, bool load_eject) {
    (void) lun; (void) power_condition; (void) load_eject;
    /* Host unmount / eject: commit any pending cached sector to flash. */
    if (!start) {
        flush_cache();
    }
    return true;
}

/* Host-level unmount: make sure dirty data is persisted. */
void tud_umount_cb(void)
{
    flush_cache();
}

/* Host mounted the MSC volume: report so the console shows the enumerating host. */
void tud_mount_cb(void)
{
    BSP_UART_Printf(" USB MSC mounted by host (Quad I/O active)\r\n");
}
