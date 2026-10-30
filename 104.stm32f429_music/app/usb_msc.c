/**
 ******************************************************************************
 * @file    usb_msc.c
 * @brief   USB mass-storage device on the microSD card (U-disk mode).
 *
 *  What the user sees
 *  ------------------
 *    plug the USB cable  ->  the host enumerates the board as a removable
 *                            drive, the screen switches to the transfer page
 *                            (capacity / used space) and the card is handed to
 *                            the PC
 *    pull the cable      ->  the card comes back, the player rescans and the
 *                            music page returns
 *
 *  Hand-over protocol (why it is not just a flag)
 *  ----------------------------------------------
 *  The PC reads and writes raw sectors through SCSI; FatFs and the SDIO polled
 *  driver are not re-entrant, so exactly one owner may exist.  The transition is
 *  therefore a four-step handshake, driven from the USB task:
 *
 *    1. tud_mount_cb()                  host has enumerated us (flag only)
 *    2. audio_player_set_usb_mode(true) player aborts at the next DMA boundary
 *                                       (~23 ms) and closes its file
 *    3. wait for audio_player_is_parked()
 *    4. fs_usb_session_begin()          takes the FatFs mutex and KEEPS it, so
 *                                       any other task blocks in fs_lock()
 *
 *  Only after step 4 is s_owned set, which is what the MSC callbacks test.  The
 *  host sees "unit not ready" while the handshake runs, retries, and gets its
 *  first good answer a few tens of milliseconds later -- no timeouts, no reset.
 *
 *  Going back is the same list in reverse: drop s_owned, fs_usb_session_end(),
 *  re-read the FAT (the host may have written anything), rescan the playlist,
 *  release the player.
 *
 *  Disconnect detection
 *  --------------------
 *  tud_umount_cb() fires on bus reset / unplug and is the primary trigger.  A
 *  poll of tud_mounted() in the task loop backs it up, so a missed event can
 *  never leave the player locked out of its own card.
 ******************************************************************************
 */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "tusb.h"
#include "class/msc/msc_device.h"

#include "usb_msc.h"
#include "audio_player.h"
#include "bsp_sdio.h"
#include "bsp_usb_device.h"
#include "fs_diskio.h"
#include "log.h"
#include "playlist.h"
#include "sd_card.h"

/* The UI runs at idle+2, the audio task at idle+3.  Same level as the UI keeps
 * the polled SDIO bursts from starving the screen refresh. */
#define USB_MSC_TASK_PRIO (tskIDLE_PRIORITY + 2)
#define USB_MSC_TASK_STACK 1024U

/* How long the player gets to stop before the card is taken anyway.  The normal
 * case is one DMA buffer (23 ms at 44.1 kHz/16-bit); the timeout only exists so
 * a stuck audio path cannot wedge the USB side forever. */
#define USB_PARK_TIMEOUT_MS 2000U

/* Longest single wait inside tud_task_ext().  Bounds the latency of the
 * tud_mounted() fallback that catches a missed unplug event. */
#define USB_TASK_IDLE_MS 20U

/* "The host is done with us" when no SCSI command has arrived for this long.
 * Covers the two cases the bus state alone cannot see: the volume was ejected
 * in Windows (bus still mounted, traffic stops) and a cable that keeps VBUS up
 * without enumerating again.  Short values cut a still-active host off between
 * two write bursts, so this is deliberately generous. */
#define USB_HOST_IDLE_MS 10000U

#define USB_SECTOR_SIZE 512U
#define USB_EP0_SIZE 64U
#define USB_EP_BULK_SIZE 64U

#define EPNUM_MSC_OUT 0x02U
#define EPNUM_MSC_IN 0x81U

/*--------------------------------------------------------------------*/
/* State                                                              */
/*--------------------------------------------------------------------*/
static volatile USB_MSC_STATE_T s_state = USB_MSC_STATE_IDLE;
static volatile uint8_t s_owned = 0U; /* MSC callbacks may touch the card */
static volatile uint32_t s_total_mb = 0U;
static volatile uint32_t s_used_mb = 0U;
static volatile bool s_evt_mount = false;
static volatile bool s_evt_umount = false;
static volatile bool s_io_error_logged = false;

/* Last time the host actually talked to the volume.  Written from the MSC
 * callbacks (all of which run inside the USB task) and read by the same task,
 * so no locking is needed. */
static volatile TickType_t s_last_cmd_tick = 0U;

static void note_host_command(void)
{
    s_last_cmd_tick = xTaskGetTickCount();
}

/* Bounce sector for the rare unaligned / partial-block request.  Owned by the
 * USB task only (every caller of the helpers below runs inside tud_task). */
static uint8_t s_bounce[USB_SECTOR_SIZE] __attribute__((aligned(4)));

/*--------------------------------------------------------------------*/
/* USB descriptors                                                    */
/*--------------------------------------------------------------------*/
static const tusb_desc_device_t s_device_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200, /* USB 2.0 */
    .bDeviceClass       = 0x00,   /* defined by the interface */
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = USB_EP0_SIZE,
    .idVendor           = 0x0483, /* STMicroelectronics */
    .idProduct          = 0x5720, /* mass-storage reference ID */
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

static const uint8_t s_config_desc[] = {
    /* 1 interface, string 0, bus powered, 100 mA, no remote wakeup */
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    /* Interface 0, string 0, bulk OUT / bulk IN */
    TUD_MSC_DESCRIPTOR(0, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, USB_EP_BULK_SIZE),
};

/* The templates are byte arrays: a mismatch against the length the configuration
 * descriptor advertises would make the host reject the device, so check it at
 * compile time rather than on a scope. */
TU_VERIFY_STATIC(sizeof(s_config_desc) == CONFIG_TOTAL_LEN,
                 "MSC configuration descriptor length mismatch");

static const char *const s_strings[] = {
    NULL, /* index 0 is the language ID, handled below */
    "STMicroelectronics",
    "STM32F429 Music Player",
    "000000000001",
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&s_device_desc;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index; /* single configuration */
    return s_config_desc;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    static uint16_t desc_str[1U + 32U];
    const char     *str;
    uint8_t         count;

    (void)langid;

    if (index == 0U)
    {
        desc_str[1] = 0x0409U; /* English (United States) */
        count       = 1U;
    }
    else
    {
        if (index >= (uint8_t)(sizeof(s_strings) / sizeof(s_strings[0])))
        {
            return NULL;
        }

        str = s_strings[index];
        count = 0U;
        while ((str[count] != '\0') && (count < 32U))
        {
            /* ASCII -> UTF-16LE */
            desc_str[1U + count] = (uint16_t)(uint8_t)str[count];
            count++;
        }
    }

    desc_str[0] = (uint16_t)(((uint16_t)TUSB_DESC_STRING << 8) |
                             (uint16_t)(2U * count + 2U));
    return desc_str;
}

/*--------------------------------------------------------------------*/
/* Sector access                                                      */
/*--------------------------------------------------------------------*/
static void report_io_error(const char *what)
{
    /* One line per session: a dead card would otherwise flood the UART at
     * sector rate. */
    if (!s_io_error_logged)
    {
        s_io_error_logged = true;
        PRINT_LOG("[USB ] %s failed (card removed?)\r\n", what);
    }
}

/**
 * @brief  Fill @p len bytes at @p dst from the card starting at @p lba + @p offset.
 */
static bool msc_read(uint8_t *dst, uint32_t lba, uint32_t offset, uint32_t len)
{
    if (len == 0U)
    {
        return true;
    }

    /* Fast path: the SCSI layer always hands us whole blocks at offset 0 (the
     * class buffer is a multiple of the block size and 4-byte aligned), so the
     * whole burst goes to the SDIO driver in one multi-block transfer. */
    if ((offset == 0U) && ((len % USB_SECTOR_SIZE) == 0U) &&
        (((uint32_t)dst & 3U) == 0U))
    {
        return (bsp_sdio_read_blocks(dst, lba, len / USB_SECTOR_SIZE) == HAL_OK)
                   ? true
                   : false;
    }

    while (len > 0U)
    {
        uint32_t chunk = USB_SECTOR_SIZE - offset;

        if (chunk > len)
        {
            chunk = len;
        }
        if (bsp_sdio_read_blocks(s_bounce, lba, 1U) != HAL_OK)
        {
            return false;
        }
        memcpy(dst, &s_bounce[offset], chunk);

        dst += chunk;
        len -= chunk;
        lba++;
        offset = 0U;
    }
    return true;
}

/**
 * @brief  Write @p len bytes from @p src at @p lba + @p offset.
 *
 *  Partial sectors need a read-modify-write: the card only accepts whole
 *  blocks, and the host may well stop in the middle of one.
 */
static bool msc_write(const uint8_t *src, uint32_t lba, uint32_t offset, uint32_t len)
{
    if (len == 0U)
    {
        return true;
    }

    if ((offset == 0U) && ((len % USB_SECTOR_SIZE) == 0U) &&
        (((uint32_t)src & 3U) == 0U))
    {
        return (bsp_sdio_write_blocks(src, lba, len / USB_SECTOR_SIZE) == HAL_OK)
                   ? true
                   : false;
    }

    while (len > 0U)
    {
        uint32_t chunk = USB_SECTOR_SIZE - offset;

        if (chunk > len)
        {
            chunk = len;
        }

        if ((offset == 0U) && (chunk == USB_SECTOR_SIZE))
        {
            /* Whole sector, just not aligned to the caller's buffer: copy into
             * the aligned bounce buffer and write it directly. */
            memcpy(s_bounce, src, USB_SECTOR_SIZE);
        }
        else
        {
            if (bsp_sdio_read_blocks(s_bounce, lba, 1U) != HAL_OK)
            {
                return false;
            }
            memcpy(&s_bounce[offset], src, chunk);
        }

        if (bsp_sdio_write_blocks(s_bounce, lba, 1U) != HAL_OK)
        {
            return false;
        }

        src += chunk;
        len -= chunk;
        lba++;
        offset = 0U;
    }
    return true;
}

/*--------------------------------------------------------------------*/
/* MSC callbacks (called from tud_task, i.e. the USB task)             */
/*--------------------------------------------------------------------*/
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize)
{
    note_host_command();

    if ((lun != 0U) || (s_owned == 0U))
    {
        return -1; /* TUD_MSC_RET_ERROR: the host gets "medium not present" */
    }
    if (!msc_read((uint8_t *)buffer, lba, offset, bufsize))
    {
        report_io_error("read10");
        return -1;
    }
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize)
{
    note_host_command();

    if ((lun != 0U) || (s_owned == 0U))
    {
        return -1;
    }
    if (!msc_write(buffer, lba, offset, bufsize))
    {
        report_io_error("write10");
        return -1;
    }
    return (int32_t)bufsize;
}

/* SCSI opcodes TinyUSB's generic layer does not implement but that real hosts
 * do send.  (msc.h only defines the ones it answers itself.) */
#define SCSI_CMD_SYNCHRONIZE_CACHE_10 0x35U
#define SCSI_CMD_VERIFY_10 0x2FU

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer,
                        uint16_t bufsize)
{
    (void)buffer;
    (void)bufsize;

    note_host_command();

    switch (scsi_cmd[0])
    {
    case SCSI_CMD_SYNCHRONIZE_CACHE_10:
    case SCSI_CMD_VERIFY_10:
        /* Every write10 already went to the card (no write cache here), so a
         * flush has nothing to do and a verify would re-read what we just
         * wrote.  Both succeed with no data phase -- failing them makes the
         * host mark the volume as write-error. */
        return 0;

    default:
        /* The generic commands TinyUSB handles (INQUIRY, READ/WRITE(10), READ
         * CAPACITY, MODE SENSE, TEST UNIT READY, ...) never reach here;
         * anything else gets STALLed with a proper sense key instead of being
         * silently "successful". */
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        return -1;
    }
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    uint32_t blocks = 0U;
    uint16_t bsize  = (uint16_t)USB_SECTOR_SIZE;

    note_host_command();

    /* Geometry comes straight from the HAL card info, not from FatFs, so it is
     * safe to answer before the key hand-over -- and it means the host caches
     * the right size even for an unformatted card. */
    if (lun == 0U)
    {
        if (bsp_sdio_get_info(&blocks, &bsize) != RT_OK)
        {
            blocks = 0U;
            bsize  = (uint16_t)USB_SECTOR_SIZE;
        }
    }

    *block_count = blocks;
    *block_size  = (bsize != 0U) ? bsize : (uint16_t)USB_SECTOR_SIZE;
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    note_host_command();

    /* Keeps the host retrying instead of failing the drive while the hand-over
     * handshake is still running. */
    if ((lun != 0U) || (s_owned == 0U))
    {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
        return false;
    }
    return true;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;
    (void)start;

    note_host_command();

    if (load_eject)
    {
        /* "Safely remove hardware" only asks us to flush; the card stays. */
        PRINT_LOG("[USB ] host ejected the volume (card still present)\r\n");
    }
    return true;
}

/*--------------------------------------------------------------------*/
/* Device events                                                      */
/*--------------------------------------------------------------------*/
void tud_mount_cb(void)
{
    /* Runs inside tud_task: only flag it, the hand-over blocks. */
    s_evt_mount = true;
}

void tud_umount_cb(void)
{
    s_evt_umount = true;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    PRINT_LOG("[USB ] bus suspend\r\n");
}

void tud_resume_cb(void)
{
    PRINT_LOG("[USB ] bus resume\r\n");
}

/*--------------------------------------------------------------------*/
/* Session hand-over                                                  */
/*--------------------------------------------------------------------*/
/**
 * @brief  Pump the device stack while waiting, so the host keeps getting
 *         "not ready" answers instead of a stalled endpoint.
 */
static void pump_usb(uint32_t ms)
{
    tud_task_ext(ms, false);
}

static void usb_session_enter(void)
{
    uint32_t waited = 0U;
    uint32_t total_mb = 0U;
    uint32_t used_mb = 0U;

    PRINT_LOG("[USB ] host enumerated, handing the SD card over\r\n");

    /* 1. Tell the player to let go, then wait for it to actually be out of the
     *    audio path (one DMA buffer in the normal case). */
    audio_player_set_usb_mode(true);
    while ((audio_player_is_parked() == false) && (waited < USB_PARK_TIMEOUT_MS))
    {
        pump_usb(5U);
        waited += 5U;
    }
    if (waited >= USB_PARK_TIMEOUT_MS)
    {
        PRINT_LOG("[USB ] player did not park in %u ms, taking the card anyway\r\n",
                  (unsigned)USB_PARK_TIMEOUT_MS);
    }

    /* 2. No card yet?  Try once -- the player normally mounts it, but it is
     *    parked now and the PC would otherwise see an empty drive. */
    if (sd_card_is_ready() == 0)
    {
        (void)sd_card_init();
    }

    /* 3. Geometry snapshot while FatFs still owns the volume (f_getfree needs
     *    the mounted FAT).  Not refreshed later: during the session the FAT
     *    belongs to the host. */
    (void)sd_card_usage_mb(&total_mb, &used_mb);
    s_total_mb = total_mb;
    s_used_mb  = used_mb;

    /* 4. Take the filesystem mutex for the whole session. */
    fs_usb_session_begin();

    s_io_error_logged = false;
    s_owned           = 1U;
    s_state           = USB_MSC_STATE_ACTIVE;
    /* The 10 s idle clock starts now, not at the last command: enumeration alone
     * can keep the bus quiet for a while before the first TEST UNIT READY. */
    s_last_cmd_tick   = xTaskGetTickCount();

    PRINT_LOG("[USB ] storage exported: %lu MB total, %lu MB used\r\n",
              (unsigned long)total_mb, (unsigned long)used_mb);
}

static void usb_session_leave(void)
{
    if (s_state != USB_MSC_STATE_ACTIVE)
    {
        return;
    }

    /* 1. Stop answering SCSI before the card changes hands. */
    s_owned = 0U;
    fs_usb_session_end();

    /* 2. The host may have created, overwritten or deleted anything: drop
     *    FatFs' cached FAT and force the player to rescan. */
    if (sd_card_remount() != RT_OK)
    {
        PRINT_LOG("[USB ] remount after the session FAILED\r\n");
    }
    playlist_clear();

    /* 3. Give the player its card back, and only then report IDLE: the UI
     *    re-opens the GBK font files when it sees this state. */
    audio_player_set_usb_mode(false);
    s_state = USB_MSC_STATE_IDLE;

    PRINT_LOG("[USB ] host released the card, back to the player\r\n");
}

/*--------------------------------------------------------------------*/
/* Task                                                               */
/*--------------------------------------------------------------------*/
static void usb_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        /* Returns early on an event, otherwise after USB_TASK_IDLE_MS. */
        tud_task_ext(pdMS_TO_TICKS(USB_TASK_IDLE_MS), false);

        if (s_evt_mount)
        {
            s_evt_mount = false;
            usb_session_enter();
        }
        if (s_evt_umount)
        {
            s_evt_umount = false;
            usb_session_leave();
        }

        /* Three ways to decide the host is gone, checked here so a missed event
         * cannot leave the card parked forever:
         *   1. tud_umount_cb() set s_evt_umount,
         *   2. the stack itself reports "not mounted" (cable pulled),
         *   3. no SCSI command for USB_HOST_IDLE_MS -- the volume was ejected,
         *      or the PC stopped using it. */
        if (s_state == USB_MSC_STATE_ACTIVE)
        {
            bool mounted = tud_mounted();

            if (!mounted)
            {
                usb_session_leave();
            }
            else if ((xTaskGetTickCount() - s_last_cmd_tick) >=
                     pdMS_TO_TICKS(USB_HOST_IDLE_MS))
            {
                PRINT_LOG("[USB ] no host traffic for %u s, releasing the card\r\n",
                          (unsigned)(USB_HOST_IDLE_MS / 1000U));
                usb_session_leave();
            }
        }
    }
}

/*--------------------------------------------------------------------*/
/* Public API                                                         */
/*--------------------------------------------------------------------*/
void usb_msc_init(void)
{
    bsp_usb_device_init();

    if (!tusb_init())
    {
        PRINT_LOG("[USB ] tusb_init FAILED\r\n");
        return;
    }
    PRINT_LOG("[USB ] device stack up (OTG FS full speed, MSC -> SD)\r\n");
}

void usb_msc_start(void)
{
    (void)xTaskCreate(usb_task, "usb", USB_MSC_TASK_STACK, NULL,
                      USB_MSC_TASK_PRIO, NULL);
}

USB_MSC_STATE_T usb_msc_state(void)
{
    return s_state;
}

void usb_msc_capacity_mb(uint32_t *total_mb, uint32_t *used_mb)
{
    if (total_mb != NULL)
    {
        *total_mb = s_total_mb;
    }
    if (used_mb != NULL)
    {
        *used_mb = s_used_mb;
    }
}
