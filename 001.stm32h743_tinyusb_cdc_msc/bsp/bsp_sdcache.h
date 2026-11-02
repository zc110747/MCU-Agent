/* ---------------------------------------------------------------------------
 * SD block cache - 128 KB write-back window in front of the block layer
 *
 * Sits *under* both FatFs (disk_interface.c) and the USB Mass Storage class
 * (msc_disk.c): every 512-byte block they read or write goes through here, so
 * the two views of the card can never disagree, even while the cache holds
 * unflushed data.
 *
 * Boot behaviour
 * --------------
 * sdcache_prewarm() runs *before* tusb_init(): it parses the MBR / boot sector
 * and loads the system area (MBR + BPB + FSInfo + reserved sectors), the FATs
 * and the FAT12/16 root directory into the cache. The host's mount sequence
 * (README sector, BPB, FAT, root dir) is then served from RAM instead of
 * costing a card round trip per sector.
 *
 * Write-back
 * ----------
 * Dirty sectors are flushed at well defined points only:
 *   - end of a USB WRITE10 transfer   -> sdcache_flush_range()
 *   - SCSI SYNCHRONIZE CACHE(10)      -> sdcache_flush()
 *   - host eject (START STOP UNIT)    -> sdcache_flush()
 *   - FatFs CTRL_SYNC (f_sync/f_close)-> sdcache_flush()
 *   - USB unmount / suspend           -> sdcache_flush()
 * Consecutive dirty sectors are merged into one multi-block card write.
 * -------------------------------------------------------------------------*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Cache capacity. 256 sectors = 128 KB: MBR + BPB + FSInfo + the leading FAT
 * sectors + root directory of a typical card. Raise to 512 for 256 KB as long
 * as RAM_D1 (AXI SRAM, 512 KB) has room - see the memory map in README.md. */
#define SDCACHE_SECTORS   256u
#define SDCACHE_BYTES     (SDCACHE_SECTORS * 512u)

typedef struct
{
    uint32_t sectors;        /* capacity, in 512 B sectors                 */
    uint32_t valid;          /* entries holding data                       */
    uint32_t dirty;          /* entries waiting for write-back             */
    uint32_t rd_hit;         /* sectors served from cache                  */
    uint32_t rd_miss;        /* sectors filled from the card               */
    uint32_t rd_direct;      /* sectors passed straight through (big read) */
    uint32_t wr_hit;         /* sectors updated in the cache               */
    uint32_t wr_miss;        /* sectors buffered on write                  */
    uint32_t wr_direct;      /* sectors written straight to the card       */
    uint32_t evict_dirty;    /* dirty entries evicted one by one           */
    uint32_t wb_ops;         /* card write operations issued by flush      */
    uint32_t wb_sectors;     /* sectors written back by flush              */
    uint32_t flush_calls;    /* sdcache_flush*() invocations               */
    uint32_t dropped;        /* dirty sectors discarded on invalidate      */
    uint32_t prewarm_secs;   /* sectors loaded by the boot prewarm         */
    uint32_t prewarm_sys;    /*   ... of which MBR + boot/reserved area    */
    uint32_t prewarm_fat;    /*   ... of which FAT / root directory        */
    int32_t  prewarm_st;     /* 0 = ok, SD_ST_ERR/no card = layout unknown */
    /* Volume layout discovered by the prewarm BPB parse (0 = unknown). */
    uint32_t part_lba;       /* first sector of the partition              */
    uint32_t rsvd;           /* reserved sectors before the first FAT      */
    uint32_t fatsz;          /* sectors per FAT                            */
    uint32_t root_secs;      /* FAT12/16 root directory, in sectors        */
    uint32_t sec_per_clus;
    uint8_t  fs_type;        /* 12 / 16 / 32, 0 = not detected             */
} SDCACHE_STATS_T;

/* Drop every entry and mark the cache usable. Call once, after sdcard_init()
 * and before the first block access. */
void sdcache_init(void);

/* True when reads/writes are actually being cached. */
bool sdcache_ready(void);

/* Parse the volume and pre-load the system area + FATs + root dir.
 * Best effort: a blank card simply leaves the leading sectors cached. */
int32_t sdcache_prewarm(void);

int32_t sdcache_read(uint8_t* buf, uint32_t lba, uint32_t count);
int32_t sdcache_write(const uint8_t* buf, uint32_t lba, uint32_t count);

/* Write back everything dirty. Returns SD_ST_OK / SD_ST_ERR. */
int32_t sdcache_flush(void);
/* Write back the dirty sectors inside [lba, lba+count). */
int32_t sdcache_flush_range(uint32_t lba, uint32_t count);

/* Throw the window away (card swapped / re-initialised). Dirty data that has
 * not been flushed yet is counted in stats.dropped - it cannot be recovered. */
void sdcache_invalidate(void);

uint32_t sdcache_dirty(void);
void     sdcache_stats(SDCACHE_STATS_T* out);

#ifdef __cplusplus
}
#endif
