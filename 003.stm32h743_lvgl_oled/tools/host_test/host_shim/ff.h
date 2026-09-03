/**
 * Minimal FatFs stand-in so the sources under Bsp/font can be compiled and
 * exercised on a PC.
 *
 * It implements only what ttf_reader.c and ctf_reader.c call, on top of stdio.
 * The point is to run the real firmware code - block cache, index addressing,
 * bounds checks, stb adapter - against the real .ctf/.ttf files before any of
 * it goes near the board.
 */
#ifndef HOST_SHIM_FF_H
#define HOST_SHIM_FF_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int   UINT;
typedef unsigned long  FSIZE_t;
typedef int            FRESULT;

#define FR_OK     0
#define FR_DENIED 1
#define FR_ERROR  2

#define FA_READ   0x01

typedef struct
{
    FILE   *fp;
    long    size;
    int     open;
} FIL;

typedef struct
{
    unsigned char fattrib;
    char          fname[256];
    FSIZE_t       fsize;
} FILINFO;

#define AM_DIR   0x10

static inline FRESULT f_open_(FIL *f, const char *path)
{
    f->fp = fopen(path, "rb");
    if (f->fp == NULL)
    {
        f->open = 0;
        f->size = 0;
        return FR_DENIED;
    }
    fseek(f->fp, 0, SEEK_END);
    f->size = ftell(f->fp);
    fseek(f->fp, 0, SEEK_SET);
    f->open = 1;
    return FR_OK;
}

static inline FRESULT f_close_(FIL *f)
{
    if (f->open && f->fp != NULL)
    {
        fclose(f->fp);
    }
    f->fp   = NULL;
    f->open = 0;
    return FR_OK;
}

static inline FRESULT f_lseek_(FIL *f, FSIZE_t off)
{
    if (!f->open || f->fp == NULL)
    {
        return FR_DENIED;
    }
    /* FatFs clamps a seek past EOF; stdio does not care. */
    if ((long)off > f->size)
    {
        return FR_DENIED;
    }
    return (fseek(f->fp, (long)off, SEEK_SET) == 0) ? FR_OK : FR_ERROR;
}

static inline FRESULT f_read_(FIL *f, void *dst, UINT len, UINT *br)
{
    size_t got;

    *br = 0;
    if (!f->open || f->fp == NULL)
    {
        return FR_DENIED;
    }
    got = fread(dst, 1, (size_t)len, f->fp);

    /* FatFs reports a short read at EOF as FR_OK with *br < btr - only a real
     * I/O failure is an error.  blkcache stores *br as the block's valid
     * length, so the last, partial block of a file must travel this path. */
    *br = (UINT)got;
    if (got == 0u && len != 0u && ferror(f->fp))
    {
        return FR_ERROR;
    }
    return FR_OK;
}

static inline FSIZE_t f_size_(const FIL *f)
{
    return (FSIZE_t)f->size;
}

static inline FRESULT f_stat_(const char *path, FILINFO *fi)
{
    FILE *fp = fopen(path, "rb");
    long  end;

    if (fp == NULL)
    {
        return FR_DENIED;
    }
    fseek(fp, 0, SEEK_END);
    end = ftell(fp);
    fclose(fp);

    fi->fattrib = 0;
    fi->fsize   = (FSIZE_t)end;
    return FR_OK;
}

/* Camel case on the firmware side, snake case here so the helpers above can be
 * declared static inline without collisions. */
#define f_open(f, p, m)    f_open_((f), (p))
#define f_close(f)         f_close_((f))
#define f_lseek(f, o)      f_lseek_((f), (o))
#define f_read(f, d, n, b) f_read_((f), (d), (n), (b))
#define f_size(f)          f_size_(f)
#define f_stat(p, fi)      f_stat_((p), (fi))

#endif /* HOST_SHIM_FF_H */
