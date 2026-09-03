/**
  ******************************************************************************
  * @file    ctf_reader.h
  * @brief   Reader for CTF index files - see ctf_format.h for the layout.
  *
  *  The whole point of this module is the lookup path:
  *
  *      code point -> L1 record -> page record -> 256-bit presence map -> entry
  *
  *  Every step that can decide "this font does not have that character" does so
  *  immediately and returns CTF_NOT_FOUND.  NOT_FOUND is a normal result, not
  *  an error: it never touches the TTF, never logs, and never raises a fault.
  *  The TTF is only consulted once an entry has actually been found.
  *
  *  The index file is never loaded into RAM.  At most:
  *    - an optional 2 KB copy of the 256-entry Level-1 table (one read, ever),
  *    - one 40-byte page record (the most recently used one),
  *    - block_count * block_size bytes of read cache.
  ******************************************************************************
  */
#ifndef __CTF_READER_H
#define __CTF_READER_H

#include <stdint.h>
#include "main.h"
#include "ff.h"
#include "blkcache.h"
#include "ctf_format.h"

/** Index accesses are tiny and highly local: 512 B x 8 = 4 KB is plenty. */
#define CTF_BLOCK_SIZE   (512u)
#define CTF_BLOCK_COUNT  8u

/** Size of the optional Level-1 shadow copy: 256 * 8 bytes. */
#define CTF_L1_SHADOW_SIZE (CTF_L1_ENTRIES * CTF_L1_SIZE)

/**
  * @brief  Result codes.
  *
  *  CTF_NOT_FOUND is a *normal* outcome - the character is simply not in this
  *  font.  Callers must not treat it as a failure.
  */
typedef enum
{
    CTF_OK = 0,
    CTF_NOT_FOUND,      /**< no such character - do not touch the TTF        */
    CTF_ERR_IO,         /**< SD / FatFs failure                              */
    CTF_ERR_FORMAT,     /**< bad magic, version, mode or geometry            */
    CTF_ERR_RANGE,      /**< an offset pointed outside the file              */
    CTF_ERR_CLOSED      /**< reader is not open                              */
} ctf_result_t;

typedef struct
{
    FIL         f;
    uint8_t     open;
    uint32_t    size;          /**< size of the .ctf file itself             */
    ctf_header_t h;

    blkcache_t  cache;

    /* Optional RAM copy of the Level-1 table - removes one read per lookup. */
    uint8_t    *l1_shadow;
    uint8_t     l1_ready;

    /* One-entry page record cache; consecutive text usually shares a page. */
    uint8_t     page_ready;
    uint32_t    page_plane;
    uint32_t    page_no;
    ctf_page_t  page;

    /* Counters. */
    uint32_t    lookups;
    uint32_t    not_found;
    uint32_t    io_errors;
} ctf_reader_t;

/**
  * @brief  Open and validate a CTF index.
  *
  *  Checks magic, version, header size, entry size and unicode mode, and that
  *  every section lies inside the file.  A CTF_TTF mismatch is *not* checked
  *  here - call ctf_verify_ttf() for that (it is a separate, slower step).
  *
  * @param  c            reader object
  * @param  path         FatFs path, e.g. "1:/SYSTEM/HarmonyOS_Sans_SC/X.ctf"
  * @param  cache_buf    CTF_BLOCK_SIZE * CTF_BLOCK_COUNT bytes
  * @param  block_size   power of two
  * @param  block_count  <= BLKCACHE_MAX_BLOCKS
  * @param  l1_shadow    optional CTF_L1_SHADOW_SIZE buffer, or NULL to always
  *                      read Level-1 records through the cache
  * @retval RT_OK when the index is usable
  */
GlobalType_t ctf_open(ctf_reader_t *c,
                      const char   *path,
                      uint8_t      *cache_buf,
                      uint32_t      block_size,
                      uint32_t      block_count,
                      uint8_t      *l1_shadow);

void ctf_close(ctf_reader_t *c);

/** 1 when the reader holds a validated index. */
int ctf_is_open(const ctf_reader_t *c);

const ctf_header_t *ctf_header(const ctf_reader_t *c);

/**
  * @brief  Look up a Unicode code point.
  *
  * @param  c     opened reader
  * @param  cp    code point (0..0xFFFFFF; anything larger is NOT_FOUND)
  * @param  out   filled in on CTF_OK
  * @retval CTF_OK, CTF_NOT_FOUND, or an error code
  */
ctf_result_t ctf_find_unicode(ctf_reader_t *c, uint32_t cp, ctf_entry_t *out);

/**
  * @brief  Confirm that a TTF is the file this index was generated from.
  *
  *  Always compares the size.  When @p do_crc is set (and the index carries a
  *  non-zero CRC) it also streams the whole file, which for a multi-MB font
  *  takes a while - hence it is opt-in.
  *
  * @retval RT_OK on match, RT_FAIL on mismatch or I/O error
  */
GlobalType_t ctf_verify_ttf(const ctf_reader_t *c, const char *ttf_path, int do_crc);

void ctf_stats(const ctf_reader_t *c,
               uint32_t *lookups, uint32_t *not_found, uint32_t *io_errors);

#endif /* __CTF_READER_H */
