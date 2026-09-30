/**
 ******************************************************************************
 * @file    lv_port_fs.c
 * @brief   LVGL file system driver on top of FatFs, with a read block cache.
 * @see     lv_port_fs.h for why this exists instead of LV_USE_FS_FATFS.
 ******************************************************************************
 */
#include "lv_port_fs.h"
#include "lvgl.h"
#include "ff.h"
#include <string.h>
#include <stdint.h>

/* Logical drive the LVGL driver is registered under, and the FatFs volume it
 * maps to.  They must stay in sync with lcd_driver_font_init() (f_mount "1:"). */
#define FS_LETTER '1'
#define FS_VOLUME "1:"

/* Concurrent open files.  The HarmonyOS engine keeps one handle per font size
 * (12/16/24/32), so four is the working set; a couple spare costs nothing. */
#define FS_MAX_FILES 6u

/* Cache geometry: 16 blocks x 512 B = 8 kB, shared by every open file. */
#define FS_BLOCK_SIZE 512u
#define FS_BLOCK_COUNT 16u

/* Longest path we will hand to FatFs: "1:" + '/' + LFN. */
#define FS_PATH_MAX (FF_MAX_LFN + 8u)

/*---------------------------------------------------------------------------*/
/* State                                                                      */
/*---------------------------------------------------------------------------*/

typedef struct
{
    FIL      f; /* FatFs file object (holds its own 512 B sector buf) */
    uint8_t  used;
    uint32_t id; /* identifies this file inside the block cache        */
} fs_file_t;

typedef struct
{
    uint8_t  data[FS_BLOCK_SIZE];
    uint32_t file_id;
    uint32_t block;
    uint32_t stamp; /* lv_port_fs clock value at the last fill            */
    uint8_t  valid;
} fs_block_t;

/* RAM FS 缓存状态：文件表、块缓存、命中统计与路径暂存 */
typedef struct
{
    fs_file_t  files[FS_MAX_FILES];
    fs_block_t blocks[FS_BLOCK_COUNT];
    uint32_t   next_id;  /* 首个分配的 id，非零默认，不可清零 */
    uint32_t   stamp;    /* lv_port_fs 时钟值，用于块 LRU      */
    uint32_t   hits;
    uint32_t   misses;
    char       path[FS_PATH_MAX]; /* fs_open() 内构建 */
} fs_cache_t;

static fs_cache_t g_fs = {.next_id = 1u};

/*---------------------------------------------------------------------------*/
/* Block cache                                                                */
/*---------------------------------------------------------------------------*/

static fs_block_t *block_find(uint32_t file_id, uint32_t block)
{
    uint32_t i;

    for (i = 0; i < FS_BLOCK_COUNT; i++)
    {
        if (g_fs.blocks[i].valid != 0u &&
            g_fs.blocks[i].file_id == file_id &&
            g_fs.blocks[i].block == block)
        {
            return &g_fs.blocks[i];
        }
    }
    return NULL;
}

/**
 * @brief  Reserve a block: an unused one if any, else the least recently used.
 */
static fs_block_t *block_victim(void)
{
    uint32_t    i;
    fs_block_t *oldest       = &g_fs.blocks[0];
    uint32_t    oldest_stamp = UINT32_MAX;

    for (i = 0; i < FS_BLOCK_COUNT; i++)
    {
        if (g_fs.blocks[i].valid == 0u)
        {
            return &g_fs.blocks[i];
        }

        /* g_fs.stamp is free running and wraps, so rank by distance from "now". */
        if ((g_fs.stamp - g_fs.blocks[i].stamp) <= oldest_stamp)
        {
            oldest_stamp = g_fs.stamp - g_fs.blocks[i].stamp;
            oldest       = &g_fs.blocks[i];
        }
    }
    return oldest;
}

/**
 * @brief  Return a cache block holding byte range [block*512, +512) of fp.
 * @retval NULL on a card error.
 */
static fs_block_t *block_load(fs_file_t *fp, uint32_t block)
{
    fs_block_t *b;
    UINT        got = 0u;

    b = block_find(fp->id, block);
    if (b != NULL)
    {
        g_fs.hits++;
        b->stamp = ++g_fs.stamp;
        return b;
    }

    g_fs.misses++;
    b = block_victim();

    if (f_lseek(&fp->f, (FSIZE_t)block * FS_BLOCK_SIZE) != FR_OK)
    {
        return NULL;
    }
    if (f_read(&fp->f, b->data, FS_BLOCK_SIZE, &got) != FR_OK)
    {
        b->valid = 0u;
        return NULL;
    }

    /* Short read at end of file: zero the tail so a rasteriser that walks off
     * the end sees deterministic zeros instead of the previous block. */
    if (got < FS_BLOCK_SIZE)
    {
        memset(b->data + got, 0, FS_BLOCK_SIZE - got);
    }

    b->file_id = fp->id;
    b->block   = block;
    b->valid   = 1u;
    b->stamp   = ++g_fs.stamp;

    return b;
}

/*---------------------------------------------------------------------------*/
/* LVGL driver callbacks                                                      */
/*---------------------------------------------------------------------------*/

static void *fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);

    uint32_t   i;
    fs_file_t *fp = NULL;

    /* Read only: nothing here ever writes to the card. */
    if (mode != LV_FS_MODE_RD)
    {
        return NULL;
    }
    if (path == NULL || path[0] == '\0')
    {
        return NULL;
    }

    for (i = 0; i < FS_MAX_FILES; i++)
    {
        if (g_fs.files[i].used == 0u)
        {
            fp = &g_fs.files[i];
            break;
        }
    }
    if (fp == NULL)
    {
        return NULL;
    }

    /* LVGL hands us "/SYSTEM/..." - put the volume back or FatFs would look at
     * logical drive 0, which has no file system mounted. */
    if ((strlen(FS_VOLUME) + strlen(path)) >= sizeof(g_fs.path))
    {
        return NULL;
    }
    strcpy(g_fs.path, FS_VOLUME);
    strcat(g_fs.path, path);

    if (f_open(&fp->f, g_fs.path, FA_READ) != FR_OK)
    {
        return NULL;
    }

    fp->used = 1u;
    fp->id   = g_fs.next_id++;
    if (g_fs.next_id == 0u)
    {
        g_fs.next_id = 1u;
    }

    return fp;
}

static lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *file_p)
{
    LV_UNUSED(drv);

    fs_file_t *fp = (fs_file_t *)file_p;

    (void)f_close(&fp->f);
    fp->used = 0u;

    /* Drop this file's blocks: a later open may reuse the slot with a new id,
     * but stale entries would only waste space - invalidate them anyway. */
    {
        uint32_t i;
        for (i = 0; i < FS_BLOCK_COUNT; i++)
        {
            if (g_fs.blocks[i].valid != 0u && g_fs.blocks[i].file_id == fp->id)
            {
                g_fs.blocks[i].valid = 0u;
            }
        }
    }

    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file_p, void *buf,
                           uint32_t btr, uint32_t *br)
{
    LV_UNUSED(drv);

    fs_file_t *fp   = (fs_file_t *)file_p;
    uint8_t   *dst  = (uint8_t *)buf;
    uint32_t   pos  = (uint32_t)f_tell(&fp->f);
    uint32_t   left = btr;

    while (left != 0u)
    {
        uint32_t    blk   = pos / FS_BLOCK_SIZE;
        uint32_t    off   = pos - (blk * FS_BLOCK_SIZE);
        uint32_t    chunk = FS_BLOCK_SIZE - off;
        fs_block_t *b;

        if (chunk > left)
        {
            chunk = left;
        }

        b = block_load(fp, blk);
        if (b == NULL)
        {
            *br = btr - left;
            return LV_FS_RES_UNKNOWN;
        }

        memcpy(dst, b->data + off, chunk);

        dst += chunk;
        pos += chunk;
        left -= chunk;
    }

    /* Keep the FatFs pointer in sync: block_load() seeks it all over the file. */
    (void)f_lseek(&fp->f, (FSIZE_t)pos);

    *br = btr;
    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos,
                           lv_fs_whence_t whence)
{
    LV_UNUSED(drv);

    fs_file_t *fp = (fs_file_t *)file_p;
    FSIZE_t    target;

    switch (whence)
    {
    case LV_FS_SEEK_SET:
        target = (FSIZE_t)pos;
        break;
    case LV_FS_SEEK_CUR:
        target = (FSIZE_t)(f_tell(&fp->f) + pos);
        break;
    case LV_FS_SEEK_END:
        target = (FSIZE_t)(f_size(&fp->f) + pos);
        break;
    default:
        return LV_FS_RES_INV_PARAM;
    }

    return (f_lseek(&fp->f, target) == FR_OK) ? LV_FS_RES_OK
                                              : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    LV_UNUSED(drv);

    fs_file_t *fp = (fs_file_t *)file_p;

    *pos_p = (uint32_t)f_tell(&fp->f);
    return LV_FS_RES_OK;
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                 */
/*---------------------------------------------------------------------------*/

void lv_port_fs_init(void)
{
    static lv_fs_drv_t drv;

    memset(g_fs.files, 0, sizeof(g_fs.files));
    memset(g_fs.blocks, 0, sizeof(g_fs.blocks));
    g_fs.next_id = 1u;
    g_fs.stamp   = 0u;
    g_fs.hits    = 0u;
    g_fs.misses  = 0u;

    lv_fs_drv_init(&drv);

    drv.letter     = FS_LETTER;
    drv.cache_size = 0u; /* we do our own, multi-block caching */
    drv.open_cb    = fs_open;
    drv.close_cb   = fs_close;
    drv.read_cb    = fs_read;
    drv.seek_cb    = fs_seek;
    drv.tell_cb    = fs_tell;

    lv_fs_drv_register(&drv);
}

void lv_port_fs_stats(uint32_t *hits, uint32_t *misses)
{
    if (hits != NULL)
    {
        *hits = g_fs.hits;
    }
    if (misses != NULL)
    {
        *misses = g_fs.misses;
    }
}
