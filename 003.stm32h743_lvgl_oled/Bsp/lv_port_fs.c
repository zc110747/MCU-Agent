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
#define FS_LETTER       '1'
#define FS_VOLUME       "1:"

/* Concurrent open files.  The HarmonyOS engine keeps one handle per font size
 * (12/16/24/32), so four is the working set; a couple spare costs nothing. */
#define FS_MAX_FILES    6u

/* Cache geometry: 16 blocks x 512 B = 8 kB, shared by every open file. */
#define FS_BLOCK_SIZE   512u
#define FS_BLOCK_COUNT  16u

/* Longest path we will hand to FatFs: "1:" + '/' + LFN. */
#define FS_PATH_MAX     (FF_MAX_LFN + 8u)

/*---------------------------------------------------------------------------*/
/* State                                                                      */
/*---------------------------------------------------------------------------*/

typedef struct
{
    FIL      f;         /* FatFs file object (holds its own 512 B sector buf) */
    uint8_t  used;
    uint32_t id;        /* identifies this file inside the block cache        */
} fs_file_t;

typedef struct
{
    uint8_t  data[FS_BLOCK_SIZE];
    uint32_t file_id;
    uint32_t block;
    uint32_t stamp;     /* lv_port_fs clock value at the last fill            */
    uint8_t  valid;
} fs_block_t;

static fs_file_t  s_files[FS_MAX_FILES];
static fs_block_t s_blocks[FS_BLOCK_COUNT];

static uint32_t   s_next_id = 1u;
static uint32_t   s_stamp   = 0u;
static uint32_t   s_hits;
static uint32_t   s_misses;

/* Built in fs_open(): LVGL strips the drive letter before calling us. */
static char       s_path[FS_PATH_MAX];

/*---------------------------------------------------------------------------*/
/* Block cache                                                                */
/*---------------------------------------------------------------------------*/

static fs_block_t *block_find(uint32_t file_id, uint32_t block)
{
    uint32_t i;

    for (i = 0; i < FS_BLOCK_COUNT; i++)
    {
        if (s_blocks[i].valid != 0u &&
            s_blocks[i].file_id == file_id &&
            s_blocks[i].block == block)
        {
            return &s_blocks[i];
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
    fs_block_t *oldest = &s_blocks[0];
    uint32_t    oldest_stamp = UINT32_MAX;

    for (i = 0; i < FS_BLOCK_COUNT; i++)
    {
        if (s_blocks[i].valid == 0u)
        {
            return &s_blocks[i];
        }

        /* s_stamp is free running and wraps, so rank by distance from "now". */
        if ((s_stamp - s_blocks[i].stamp) <= oldest_stamp)
        {
            oldest_stamp = s_stamp - s_blocks[i].stamp;
            oldest       = &s_blocks[i];
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
        s_hits++;
        b->stamp = ++s_stamp;
        return b;
    }

    s_misses++;
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
    b->stamp   = ++s_stamp;

    return b;
}

/*---------------------------------------------------------------------------*/
/* LVGL driver callbacks                                                      */
/*---------------------------------------------------------------------------*/

static void *fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);

    uint32_t    i;
    fs_file_t  *fp = NULL;

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
        if (s_files[i].used == 0u)
        {
            fp = &s_files[i];
            break;
        }
    }
    if (fp == NULL)
    {
        return NULL;
    }

    /* LVGL hands us "/SYSTEM/..." - put the volume back or FatFs would look at
     * logical drive 0, which has no file system mounted. */
    if ((strlen(FS_VOLUME) + strlen(path)) >= sizeof(s_path))
    {
        return NULL;
    }
    strcpy(s_path, FS_VOLUME);
    strcat(s_path, path);

    if (f_open(&fp->f, s_path, FA_READ) != FR_OK)
    {
        return NULL;
    }

    fp->used = 1u;
    fp->id   = s_next_id++;
    if (s_next_id == 0u)
    {
        s_next_id = 1u;
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
            if (s_blocks[i].valid != 0u && s_blocks[i].file_id == fp->id)
            {
                s_blocks[i].valid = 0u;
            }
        }
    }

    return LV_FS_RES_OK;
}

static lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file_p, void *buf,
                           uint32_t btr, uint32_t *br)
{
    LV_UNUSED(drv);

    fs_file_t *fp  = (fs_file_t *)file_p;
    uint8_t   *dst = (uint8_t *)buf;
    uint32_t   pos = (uint32_t)f_tell(&fp->f);
    uint32_t   left = btr;

    while (left != 0u)
    {
        uint32_t     blk   = pos / FS_BLOCK_SIZE;
        uint32_t     off   = pos - (blk * FS_BLOCK_SIZE);
        uint32_t     chunk = FS_BLOCK_SIZE - off;
        fs_block_t  *b;

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

        dst  += chunk;
        pos  += chunk;
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
        case LV_FS_SEEK_SET: target = (FSIZE_t)pos;                       break;
        case LV_FS_SEEK_CUR: target = (FSIZE_t)(f_tell(&fp->f) + pos);    break;
        case LV_FS_SEEK_END: target = (FSIZE_t)(f_size(&fp->f) + pos);    break;
        default:             return LV_FS_RES_INV_PARAM;
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

    memset(s_files, 0, sizeof(s_files));
    memset(s_blocks, 0, sizeof(s_blocks));
    s_next_id = 1u;
    s_stamp   = 0u;
    s_hits    = 0u;
    s_misses  = 0u;

    lv_fs_drv_init(&drv);

    drv.letter     = FS_LETTER;
    drv.cache_size = 0u;        /* we do our own, multi-block caching */
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
        *hits = s_hits;
    }
    if (misses != NULL)
    {
        *misses = s_misses;
    }
}
