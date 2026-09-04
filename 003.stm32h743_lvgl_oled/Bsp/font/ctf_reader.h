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
  *  The index file is never loaded into RAM.  What *is* pinned in RAM is the
  *  small, fixed-size front of the index - see ctf_load_resident():
  *    - the TTF table directory  (table_count * 12 B, 11 tables in practice),
  *    - the Level-1 plane table  (256 * 8 B = 2 KB, one read ever),
  *    - the Level-2 page table   (page_count * 40 B) when the caller's pool is
  *      big enough for all of it.
  *  Everything else - the entry table, which is the bulk of the file - stays on
  *  the card and is reached through block_count * block_size of read cache.
  *
  *  With all three resident, a lookup that ends in NOT_FOUND does zero SD
  *  access: the presence bit is tested in RAM.  A lookup that succeeds costs
  *  exactly one cached read (the 24-byte entry).
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

/** Size of the optional Level-1 shadow copy: 256 * 8 = 2048 bytes. */
#define CTF_L1_SHADOW_SIZE (CTF_L1_ENTRIES * CTF_L1_SIZE)

/**
  * Resident TTF table directory.  ctf_header_parse() already rejects anything
  * above 64 tables, so this array can never be overrun; 64 * 12 = 768 bytes.
  */
#define CTF_TABLE_RESIDENT_MAX  64u

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

/**
  * @brief  What the reader managed to pin in RAM, and what it cost.
  *
  *  Reported so the boot log can state the real figure instead of a guess: the
  *  page table is the only variable part, and it depends on how many Unicode
  *  planes the font actually covers.
  */
typedef struct
{
    uint32_t table_bytes;    /**< TTF table directory held in RAM             */
    uint32_t l1_bytes;       /**< Level-1 plane table held in RAM             */
    uint32_t page_bytes;     /**< Level-2 page table held in RAM              */
    uint32_t total_bytes;    /**< sum of the three                            */
    uint32_t page_needed;    /**< bytes the full page table would take        */
    uint32_t page_capacity;  /**< bytes the caller's pool can hold            */
    uint32_t entry_bytes;    /**< entry table left on the card, for context   */
    uint8_t  table_resident;
    uint8_t  l1_resident;
    uint8_t  page_resident;  /**< 0 = pool too small, pages come from cache   */
} ctf_resident_t;

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

    /* Resident TTF table directory: where each TrueType table lives in the
     * .ttf.  Loaded once so nothing has to re-read the sfnt directory. */
    ctf_table_t tables[CTF_TABLE_RESIDENT_MAX];
    uint32_t    table_count;
    uint8_t     table_ready;

    /* Resident Level-2 page table.  When page_all_ready is set, every page
     * record is in RAM and the presence test never touches the card. */
    uint8_t    *page_pool;
    uint32_t    page_pool_size;
    uint32_t    page_bytes;    /**< valid bytes inside page_pool             */
    uint8_t     page_all_ready;

    /* One-entry page record cache; consecutive text usually shares a page. */
    uint8_t     page_ready;
    uint32_t    page_plane;
    uint32_t    page_no;
    ctf_page_t  page;

    /* Counters. */
    uint32_t    lookups;
    uint32_t    not_found;
    uint32_t    io_errors;
    uint32_t    page_ram_hits;  /**< page records served from the RAM copy   */
    uint32_t    page_sd_reads;  /**< page records that went to the cache     */
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

/**
  * @brief  Pin the front of the index in RAM.  Call once, after ctf_open().
  *
  *  Loads the TTF table directory and, if @p page_pool can hold all of it, the
  *  whole Level-2 page table.  The Level-1 table is already resident when
  *  ctf_open() was given an l1_shadow buffer.
  *
  *  When the page table is resident every non-empty plane's page_offset is
  *  checked to lie inside the pooled region, so the lookup path can subtract
  *  page_index_offset without any risk of wrapping.
  *
  *  A pool that is too small is not an error: the reader keeps using the block
  *  cache for page records and says so through @p out.
  *
  * @param  c               opened reader
  * @param  page_pool       buffer for the page table, or NULL to skip it
  * @param  page_pool_size  bytes available in @p page_pool
  * @param  out             optional, filled with what ended up resident
  * @retval RT_OK when the index is usable afterwards (always, unless I/O fails
  *         while reading the table directory)
  */
GlobalType_t ctf_load_resident(ctf_reader_t   *c,
                               uint8_t        *page_pool,
                               uint32_t        page_pool_size,
                               ctf_resident_t *out);

/** Fill @p out with the current resident footprint.  Safe before loading. */
void ctf_resident_info(const ctf_reader_t *c, ctf_resident_t *out);

/** Total bytes this reader holds in RAM for the index itself. */
uint32_t ctf_resident_bytes(const ctf_reader_t *c);

/**
  * @brief  Look up a TrueType table by tag, e.g. CTF_TAG('g','l','y','f').
  * @retval NULL when the directory is not resident or the tag is absent.
  */
const ctf_table_t *ctf_find_table(const ctf_reader_t *c, uint32_t tag);

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

/**
  * @brief  Where page records came from.
  *
  *  With the page table resident @p sd_reads must stay at 0 for the whole run;
  *  that is the observable proof the second hop never reaches the card.
  */
void ctf_page_stats(const ctf_reader_t *c,
                    uint32_t *ram_hits, uint32_t *sd_reads);

#endif /* __CTF_READER_H */
