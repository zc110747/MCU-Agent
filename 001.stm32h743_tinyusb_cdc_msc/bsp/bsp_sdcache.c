/* ---------------------------------------------------------------------------
 * SD block cache - 128 KB write-back window (see bsp_sdcache.h)
 *
 * Memory
 * ------
 * The 128 KB data window and a 32 KB staging buffer live in AXI SRAM
 * (.ram_d1, NOLOAD) - they do not fit in the 128 KB DTCM next to the USB
 * FIFOs. The tag array stays in DTCM: it is what the CPU touches on every
 * access, and it is small. No DMA is involved (SDMMC runs in polling mode and
 * DWC2 in FIFO/slave mode), so plain CPU copies keep cache and memory
 * coherent; nothing here needs cleaning or invalidating.
 *
 * Mapping
 * -------
 * 64 sets x 4 ways, set = lba % 64. Consecutive sectors land in consecutive
 * sets, so a linear run spreads evenly instead of fighting over a single set,
 * and the prewarmed system area never collides with itself.
 *
 * Policy
 * ------
 *  - read : hit -> copy from the window. A miss run of <= SDC_ALLOC_MAX
 *           sectors is filled into the window (metadata locality). A longer
 *           run is passed straight to the card: a 32 KB bulk read must not
 *           evict the FAT window it will need again on the next mount.
 *  - write: hit -> update in place and mark dirty. A miss run of
 *           <= SDC_ALLOC_MAX sectors is buffered; a longer run goes straight
 *           to the card (512 B writes run at ~147 KB/s on this board, 32 KB
 *           writes at ~921 KB/s, so streaming must not be chopped up).
 *  - dirty sectors leave through flush only, coalesced into multi-block card
 *    writes.
 * -------------------------------------------------------------------------*/

#include "bsp_sdcache.h"
#include "bsp_sdcard.h"

#include <string.h>

#define SDC_SECTOR_SIZE   512u
#define SDC_SETS          64u
#define SDC_WAYS          4u
#define SDC_STAGE_SECTORS 64u   /* 32 KB: coalescing / prewarm staging     */
#define SDC_ALLOC_MAX     8u    /* miss runs up to 8 sectors are cached    */
#define SDC_SYSTEM_MAX    64u   /* cap for the reserved area prewarm       */
#define SDC_LBA_MAX       0xFFFFFFFFu

#if (SDCACHE_SECTORS != (SDC_SETS * SDC_WAYS))
#error "SDCACHE_SECTORS must equal SDC_SETS * SDC_WAYS"
#endif
#if (SDCACHE_SECTORS % SDC_STAGE_SECTORS) != 0
#error "SDCACHE_SECTORS must be a multiple of SDC_STAGE_SECTORS"
#endif

typedef struct
{
    uint32_t tag;       /* LBA while valid                   */
    uint32_t stamp;     /* LRU stamp, larger = more recent   */
    uint8_t  valid;
    uint8_t  dirty;
    uint16_t reserved;
} SDC_ENTRY_T;

/* ---- data window (AXI SRAM) ------------------------------------------- */
static uint8_t g_sdc_data[SDCACHE_SECTORS][SDC_SECTOR_SIZE]
    __attribute__((section(".ram_d1"), aligned(32)));
static uint8_t g_sdc_stage[SDC_STAGE_SECTORS * SDC_SECTOR_SIZE]
    __attribute__((section(".ram_d1"), aligned(32)));

/* ---- tags (DTCM) ------------------------------------------------------- */
static SDC_ENTRY_T     g_sdc_tag[SDCACHE_SECTORS];
static SDCACHE_STATS_T g_sdc_stats;
static uint32_t        g_sdc_stamp;
static uint32_t        g_sdc_fill;      /* sectors loaded by the prewarm */
static bool            g_sdc_ready;

/* ------------------------------------------------------------------------ */
/* Slot management                                                          */
/* ------------------------------------------------------------------------ */
static uint32_t sdc_set_base(uint32_t lba)
{
    return (lba % SDC_SETS) * SDC_WAYS;
}

static int32_t sdc_lookup(uint32_t lba)
{
    uint32_t const base = sdc_set_base(lba);

    for (uint32_t w = 0; w < SDC_WAYS; w++)
    {
        uint32_t const idx = base + w;
        if (g_sdc_tag[idx].valid && g_sdc_tag[idx].tag == lba) return (int32_t) idx;
    }
    return -1;
}

/* Free slot in the set: an invalid way first, otherwise the least recently
 * used one. A dirty victim is written back before it is reused, so a cached
 * sector never disappears without reaching the card. */
static int32_t sdc_alloc(uint32_t lba)
{
    uint32_t const base   = sdc_set_base(lba);
    uint32_t       victim = base + SDC_WAYS;
    uint32_t       oldest = 0xFFFFFFFFu;

    for (uint32_t w = 0; w < SDC_WAYS; w++)
    {
        uint32_t const idx = base + w;
        if (!g_sdc_tag[idx].valid)
        {
            victim = idx;
            break;
        }
        if (g_sdc_tag[idx].stamp < oldest)
        {
            oldest = g_sdc_tag[idx].stamp;
            victim = idx;
        }
    }
    if (victim >= (base + SDC_WAYS)) return -1;   /* cannot happen */

    if (g_sdc_tag[victim].valid && g_sdc_tag[victim].dirty)
    {
        uint32_t const old_lba = g_sdc_tag[victim].tag;
        if (sdcard_raw_write_blocks(g_sdc_data[victim], old_lba, 1) != SD_ST_OK)
        {
            return -1;                            /* keep the victim, report */
        }
        g_sdc_stats.evict_dirty++;
        g_sdc_stats.wb_ops++;
        g_sdc_stats.wb_sectors++;
        sd_trace_add(SD_TR_FWDIRTY, 1, 1, old_lba);
    }

    g_sdc_tag[victim].valid = 1;
    g_sdc_tag[victim].dirty = 0;
    g_sdc_tag[victim].tag   = lba;
    g_sdc_tag[victim].stamp = ++g_sdc_stamp;
    return (int32_t) victim;
}

static void sdc_touch(int32_t idx)
{
    g_sdc_tag[idx].stamp = ++g_sdc_stamp;
}

/* Length of the run of consecutive sectors starting at lba that are not in
 * the window, bounded by limit. */
static uint32_t sdc_miss_run(uint32_t lba, uint32_t limit)
{
    uint32_t n = 0;
    while (n < limit && sdc_lookup(lba + n) < 0) n++;
    return n;
}

/* ------------------------------------------------------------------------ */
/* Init / readiness                                                         */
/* ------------------------------------------------------------------------ */
void sdcache_init(void)
{
    memset(g_sdc_tag, 0, sizeof(g_sdc_tag));
    memset(&g_sdc_stats, 0, sizeof(g_sdc_stats));
    g_sdc_stamp = 0;
    g_sdc_fill  = 0;
    g_sdc_ready = true;
}

bool sdcache_ready(void)
{
    return g_sdc_ready && sdcard_present();
}

void sdcache_invalidate(void)
{
    if (!g_sdc_ready) return;

    uint32_t dropped = 0;
    for (uint32_t i = 0; i < SDCACHE_SECTORS; i++)
    {
        if (g_sdc_tag[i].valid && g_sdc_tag[i].dirty) dropped++;
    }
    memset(g_sdc_tag, 0, sizeof(g_sdc_tag));
    g_sdc_fill = 0;

    if (dropped != 0u)
    {
        g_sdc_stats.dropped += dropped;
        sd_trace_add(SD_TR_FWDIRTY, 2, (uint16_t) dropped, 0);
    }
}

/* ------------------------------------------------------------------------ */
/* Read                                                                     */
/* ------------------------------------------------------------------------ */
int32_t sdcache_read(uint8_t* buf, uint32_t lba, uint32_t count)
{
    if (!g_sdc_ready || count == 0u) return SD_ST_OK;

    uint32_t done = 0;
    while (done < count)
    {
        uint32_t const cur = lba + done;
        int32_t        idx = sdc_lookup(cur);

        if (idx >= 0)
        {
            memcpy(buf + (size_t) done * SDC_SECTOR_SIZE,
                   g_sdc_data[idx], SDC_SECTOR_SIZE);
            sdc_touch(idx);
            g_sdc_stats.rd_hit++;
            done++;
            continue;
        }

        uint32_t const run = sdc_miss_run(cur, count - done);

        if (run <= SDC_ALLOC_MAX)
        {
            idx = sdc_alloc(cur);
            if (idx < 0)
            {
                /* No free way and the dirty victim would not flush: the
                 * caller still gets its data, straight from the card. */
                if (sdcard_raw_read_blocks(buf + (size_t) done * SDC_SECTOR_SIZE,
                                           cur, run) != SD_ST_OK) return SD_ST_ERR;
                g_sdc_stats.rd_direct += run;
                done += run;
                continue;
            }

            uint32_t k = 0;
            while (k < run)
            {
                int32_t const slot = (k == 0u) ? idx : sdc_alloc(cur + k);
                if (slot < 0) break;

                if (sdcard_raw_read_blocks(g_sdc_data[slot], cur + k, 1) != SD_ST_OK)
                {
                    g_sdc_tag[slot].valid = 0;
                    if (k == 0u) return SD_ST_ERR;
                    break;
                }
                memcpy(buf + (size_t) (done + k) * SDC_SECTOR_SIZE,
                       g_sdc_data[slot], SDC_SECTOR_SIZE);
                g_sdc_stats.rd_miss++;
                k++;
            }
            done += k;
            continue;
        }

        /* Long run: stream it, keep the window for metadata. */
        if (sdcard_raw_read_blocks(buf + (size_t) done * SDC_SECTOR_SIZE,
                                   cur, run) != SD_ST_OK) return SD_ST_ERR;
        g_sdc_stats.rd_direct += run;
        done += run;
    }
    return SD_ST_OK;
}

/* ------------------------------------------------------------------------ */
/* Write                                                                    */
/* ------------------------------------------------------------------------ */
int32_t sdcache_write(const uint8_t* buf, uint32_t lba, uint32_t count)
{
    if (!g_sdc_ready || count == 0u) return SD_ST_OK;

    uint32_t done = 0;
    while (done < count)
    {
        uint32_t const cur = lba + done;
        int32_t        idx = sdc_lookup(cur);

        if (idx >= 0)
        {
            memcpy(g_sdc_data[idx], buf + (size_t) done * SDC_SECTOR_SIZE,
                   SDC_SECTOR_SIZE);
            g_sdc_tag[idx].dirty = 1;
            sdc_touch(idx);
            g_sdc_stats.wr_hit++;
            done++;
            continue;
        }

        uint32_t const run = sdc_miss_run(cur, count - done);

        if (run <= SDC_ALLOC_MAX)
        {
            idx = sdc_alloc(cur);
            if (idx < 0)
            {
                if (sdcard_raw_write_blocks(buf + (size_t) done * SDC_SECTOR_SIZE,
                                            cur, run) != SD_ST_OK) return SD_ST_ERR;
                g_sdc_stats.wr_direct += run;
                done += run;
                continue;
            }

            uint32_t k = 0;
            while (k < run)
            {
                int32_t const slot = (k == 0u) ? idx : sdc_alloc(cur + k);
                if (slot < 0) break;

                memcpy(g_sdc_data[slot], buf + (size_t) (done + k) * SDC_SECTOR_SIZE,
                       SDC_SECTOR_SIZE);
                g_sdc_tag[slot].dirty = 1;
                g_sdc_stats.wr_miss++;
                k++;
            }
            done += k;
            continue;
        }

        if (sdcard_raw_write_blocks(buf + (size_t) done * SDC_SECTOR_SIZE,
                                    cur, run) != SD_ST_OK) return SD_ST_ERR;
        g_sdc_stats.wr_direct += run;
        done += run;
    }
    return SD_ST_OK;
}

/* ------------------------------------------------------------------------ */
/* Write-back                                                               */
/* ------------------------------------------------------------------------ */
/* Write back every dirty sector inside [first, last]. Dirty sectors are
 * walked in LBA order and merged into one card write for as long as they are
 * contiguous: one command, one programming cycle, up to 32 KB per transfer. */
static int32_t sdc_writeback(uint32_t first, uint32_t last)
{
    for (;;)
    {
        uint32_t best  = SDC_LBA_MAX;
        bool     found = false;

        for (uint32_t i = 0; i < SDCACHE_SECTORS; i++)
        {
            if (!g_sdc_tag[i].valid || !g_sdc_tag[i].dirty) continue;
            if (g_sdc_tag[i].tag < first || g_sdc_tag[i].tag > last) continue;
            if (g_sdc_tag[i].tag < best)
            {
                best  = g_sdc_tag[i].tag;
                found = true;
            }
        }
        if (!found) return SD_ST_OK;

        uint32_t n = 0;
        while (n < SDC_STAGE_SECTORS && ((uint64_t) best + n) <= (uint64_t) last)
        {
            int32_t const idx = sdc_lookup(best + n);
            if (idx < 0 || !g_sdc_tag[idx].dirty) break;
            memcpy(&g_sdc_stage[n * SDC_SECTOR_SIZE], g_sdc_data[idx], SDC_SECTOR_SIZE);
            n++;
        }
        if (n == 0u) return SD_ST_ERR;                 /* cannot happen */

        if (sdcard_raw_write_blocks(g_sdc_stage, best, n) != SD_ST_OK)
        {
            sd_trace_add(SD_TR_FWDIRTY, 3, (uint16_t) n, best);
            return SD_ST_ERR;
        }

        for (uint32_t k = 0; k < n; k++)
        {
            int32_t const idx = sdc_lookup(best + k);
            if (idx >= 0) g_sdc_tag[idx].dirty = 0;
        }
        g_sdc_stats.wb_ops++;
        g_sdc_stats.wb_sectors += n;
    }
}

int32_t sdcache_flush(void)
{
    if (!g_sdc_ready) return SD_ST_OK;
    g_sdc_stats.flush_calls++;

    if (sdcache_dirty() == 0u) return SD_ST_OK;

    int32_t const st = sdc_writeback(0u, SDC_LBA_MAX);
    sd_trace_add(SD_TR_FLUSH, (uint8_t) (st == SD_ST_OK ? 0 : 1), 0, 0);
    return st;
}

int32_t sdcache_flush_range(uint32_t lba, uint32_t count)
{
    if (!g_sdc_ready || count == 0u) return SD_ST_OK;

    uint64_t last = (uint64_t) lba + count - 1u;
    if (last > (uint64_t) SDC_LBA_MAX) last = SDC_LBA_MAX;

    if (sdcache_dirty() == 0u) return SD_ST_OK;    /* nothing to do at all */

    g_sdc_stats.flush_calls++;
    return sdc_writeback(lba, (uint32_t) last);
}

uint32_t sdcache_dirty(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < SDCACHE_SECTORS; i++)
    {
        if (g_sdc_tag[i].valid && g_sdc_tag[i].dirty) n++;
    }
    return n;
}

void sdcache_stats(SDCACHE_STATS_T* out)
{
    if (out == NULL) return;

    *out = g_sdc_stats;
    out->sectors = SDCACHE_SECTORS;

    uint32_t valid = 0;
    for (uint32_t i = 0; i < SDCACHE_SECTORS; i++)
    {
        if (g_sdc_tag[i].valid) valid++;
    }
    out->valid = valid;
    out->dirty = sdcache_dirty();
}

/* ------------------------------------------------------------------------ */
/* Boot prewarm                                                             */
/* ------------------------------------------------------------------------ */
typedef struct
{
    uint32_t part_lba;      /* first sector of the partition              */
    uint32_t rsvd;          /* reserved sectors before the first FAT      */
    uint32_t nfats;
    uint32_t fatsz;         /* sectors per FAT                            */
    uint32_t root_secs;     /* FAT12/16 root directory, in sectors        */
    uint32_t sec_per_clus;
    uint8_t  fs_type;
} SDC_BPB_T;

static uint32_t sdc_le16(const uint8_t* p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

static uint32_t sdc_le32(const uint8_t* p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
           ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/* MBR (or superfloppy) -> boot sector -> BPB. Only what the prewarm needs to
 * know: where the reserved area, the FATs and the root directory start. */
static int32_t sdc_parse_layout(SDC_BPB_T* b)
{
    uint8_t sec[SDC_SECTOR_SIZE];

    memset(b, 0, sizeof(*b));
    if (sdcard_raw_read_blocks(sec, 0, 1) != SD_ST_OK) return SD_ST_ERR;

    uint32_t boot = 0;
    bool const bpb_at_zero = ((sec[0] == 0xEBu) || (sec[0] == 0xE9u)) &&
                             (sdc_le16(&sec[11]) == SDC_SECTOR_SIZE);

    if (!bpb_at_zero)
    {
        if (sec[510] != 0x55u || sec[511] != 0xAAu) return SD_ST_ERR;

        for (uint32_t i = 0; i < 4u; i++)
        {
            const uint8_t* e = &sec[446u + i * 16u];
            uint8_t const type = e[4];
            if (type == 0x00u || type == 0xEEu) continue;   /* empty / protective */
            boot = sdc_le32(&e[8]);
            break;
        }
        if (boot == 0u) return SD_ST_ERR;
        if (sdcard_raw_read_blocks(sec, boot, 1) != SD_ST_OK) return SD_ST_ERR;
    }

    uint32_t const bps     = sdc_le16(&sec[11]);
    uint32_t const spc     = sec[13];
    uint32_t const rsvd    = sdc_le16(&sec[14]);
    uint32_t const nfats   = sec[16];
    uint32_t const rootent = sdc_le16(&sec[17]);
    uint32_t       fatsz   = sdc_le16(&sec[22]);
    bool const     fat32   = (fatsz == 0u);

    if (fat32) fatsz = sdc_le32(&sec[36]);

    if (bps != SDC_SECTOR_SIZE || spc == 0u || rsvd == 0u ||
        nfats == 0u || fatsz == 0u)
    {
        return SD_ST_ERR;
    }

    b->part_lba     = boot;
    b->rsvd         = rsvd;
    b->nfats        = nfats;
    b->fatsz        = fatsz;
    b->sec_per_clus = spc;
    b->root_secs    = (rootent * 32u + (SDC_SECTOR_SIZE - 1u)) / SDC_SECTOR_SIZE;
    b->fs_type      = fat32 ? 32u : (b->root_secs != 0u ? 16u : 12u);
    return SD_ST_OK;
}

/* Load [lba, lba+count) into the window, honouring the global capacity.
 * Sectors that are already cached are left alone: the callers pass overlapping
 * ranges on purpose (a superfloppy has its BPB in sector 0, which is also the
 * "MBR" range), and a duplicated tag would be two slots for one sector. */
static uint32_t sdc_preload(uint32_t lba, uint32_t count)
{
    uint32_t added = 0;
    uint32_t done  = 0;

    while (done < count && g_sdc_fill < SDCACHE_SECTORS)
    {
        uint32_t chunk = count - done;
        if (chunk > SDC_STAGE_SECTORS) chunk = SDC_STAGE_SECTORS;
        if (chunk > (SDCACHE_SECTORS - g_sdc_fill)) chunk = SDCACHE_SECTORS - g_sdc_fill;

        if (sdcard_raw_read_blocks(g_sdc_stage, lba + done, chunk) != SD_ST_OK) break;

        for (uint32_t i = 0; i < chunk; i++)
        {
            if (sdc_lookup(lba + done + i) >= 0) continue;   /* already hot */

            int32_t const idx = sdc_alloc(lba + done + i);
            if (idx < 0) continue;                           /* cannot happen */
            memcpy(g_sdc_data[idx], &g_sdc_stage[i * SDC_SECTOR_SIZE], SDC_SECTOR_SIZE);
            g_sdc_fill++;
            added++;
        }
        done += chunk;
    }
    return added;
}

int32_t sdcache_prewarm(void)
{
    SDC_BPB_T b;
    uint32_t  loaded = 0;

    if (!g_sdc_ready || !sdcard_present())
    {
        g_sdc_stats.prewarm_st = SD_ST_NO_CARD;
        return SD_ST_NO_CARD;
    }

    g_sdc_fill = 0;

    int32_t const st = sdc_parse_layout(&b);
    if (st != SD_ST_OK)
    {
        /* Blank or unknown volume: keep the MBR area hot so the format and the
         * first mount still get their reads from RAM. */
        loaded = sdc_preload(0u, 64u);
        g_sdc_stats.prewarm_secs = loaded;
        g_sdc_stats.prewarm_st   = st;
        sd_trace_add(SD_TR_PREWARM, 0, (uint16_t) loaded, (uint32_t) st);
        return st;
    }

    g_sdc_stats.fs_type      = b.fs_type;
    g_sdc_stats.part_lba     = b.part_lba;
    g_sdc_stats.rsvd         = b.rsvd;
    g_sdc_stats.fatsz        = b.fatsz;
    g_sdc_stats.root_secs    = b.root_secs;
    g_sdc_stats.sec_per_clus = b.sec_per_clus;

    uint32_t const fat_start = b.part_lba + b.rsvd;

    /* 1a. MBR (partition table) - a few sectors at the very front. */
    if (b.part_lba != 0u)
    {
        uint32_t mbr_secs = (b.part_lba < 8u) ? b.part_lba : 8u;
        uint32_t const n = sdc_preload(0u, mbr_secs);
        loaded += n;
        g_sdc_stats.prewarm_sys += n;
    }
    /* 1b. Boot sector + FSInfo + backup boot: the head of the reserved area.
     * The reserved area itself can be megabytes (this card: 2226 sectors) but
     * nothing past its first sectors is ever read, so it must not be allowed
     * to swallow the window - the FAT needs that room far more. */
    uint32_t boot_secs = (b.rsvd < SDC_SYSTEM_MAX) ? b.rsvd : SDC_SYSTEM_MAX;
    {
        uint32_t const n = sdc_preload(b.part_lba, boot_secs);
        loaded += n;
        g_sdc_stats.prewarm_sys += n;
    }

    /* 2. FAT 1, with everything that is left: a directory scan walks it. */
    {
        uint32_t const n = sdc_preload(fat_start, b.fatsz);
        loaded += n;
        g_sdc_stats.prewarm_fat += n;
    }
    /* 3. FAT 2 (mirror) and the FAT12/16 root directory, if room remains. */
    if (b.nfats >= 2u)
    {
        uint32_t const n = sdc_preload(fat_start + b.fatsz, b.fatsz);
        loaded += n;
        g_sdc_stats.prewarm_fat += n;
    }
    if (b.root_secs != 0u)
    {
        uint32_t const n = sdc_preload(fat_start + b.nfats * b.fatsz, b.root_secs);
        loaded += n;
        g_sdc_stats.prewarm_fat += n;
    }

    g_sdc_stats.prewarm_secs = loaded;
    g_sdc_stats.prewarm_st   = SD_ST_OK;
    sd_trace_add(SD_TR_PREWARM, 1, (uint16_t) loaded, b.part_lba);
    return SD_ST_OK;
}
