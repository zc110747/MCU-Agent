/**
  ******************************************************************************
  * @file    sram_pool.h
  * @brief   Dynamic allocator over the STM32H743's "spare" SRAM banks
  *          (DTCM and RAM_D2) so they are only occupied while an app needs
  *          them and are free for other apps (e.g. a camera) the rest of the
  *          time.
  *
  *  Why this exists
  *  ----------------
  *    The NES emulator used to pin ~80 kB of machine state in DTCM and a 256 kB
  *    ROM image in RAM_D2 as link-time statics, so those ~338 kB were locked
  *    up forever even when no game was running.  This pool hands those blocks
  *    out at runtime and reclaims them on close, leaving the banks available
  *    to whatever page is active.
  *
  *  Design
  *  ------
  *    One implicit free list per region, boundary-tagged (8-byte header +
  *    8-byte footer holding size|used).  Payload is therefore 8-byte aligned.
  *    A used prologue anchors the front and a used 0-size epilogue terminates
  *    the scan.  Free blocks are coalesced on release, so open/close cycles
  *    never fragment.  The pool is private to the firmware (single thread, no
  *    IRQs touch it), so no locking is needed.
  ******************************************************************************
  */
#ifndef __SRAM_POOL_H
#define __SRAM_POOL_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    SRAM_REGION_DTCM = 0,   /**< 0x20000000, 128 kB, TCM (non-cacheable) */
    SRAM_REGION_D2,         /**< 0x30000000, 288 kB, write-back cacheable */
    SRAM_REGION_COUNT
} sram_region_t;

/** Initialise both region arenas.  Idempotent; safe to call more than once. */
void sram_pool_init(void);

/** Allocate `size` bytes from `region`, aligned to `align` (rounded up to 8).
 *  Returns NULL if the region cannot satisfy the request. */
void *sram_alloc(sram_region_t region, size_t size, size_t align);

/** Return a block previously obtained from sram_alloc().  NULL is ignored. */
void sram_free(sram_region_t region, void *ptr);

/** Bytes currently free (payload) in a region - handy for the `status` cmd. */
size_t sram_free_bytes(sram_region_t region);

/** Total bytes managed by a region. */
size_t sram_total_bytes(sram_region_t region);

/**
 *  Walk a region and verify allocator invariants:
 *    - every block's header word equals its footer word,
 *    - blocks are contiguous (next header sits exactly at current + size),
 *    - the walk terminates exactly at the epilogue (base + total - BLK_META).
 *  Returns the free payload bytes and sets *ok to 0 on any corruption.
 *  Used by the stress test and the `sram check` console command.
 */
size_t sram_check(sram_region_t region, int *ok);

/**
 *  Enumerate the payload pointers of every *live* (allocated, used) block in a
 *  region.  The prologue and epilogue are skipped, as is any free block.
 *  Writes at most `max` pointers into `out` and returns the total number of
 *  live blocks (which may exceed `max`).
 *
 *  This is what makes a randomized stress test safe: after a free coalesces
 *  neighbours, the caller re-walks to get the *current* live set instead of
 *  holding on to a stale pointer that now sits inside a merged free block
 *  (which would otherwise cause a double-free / heap corruption).
 */
int sram_walk(sram_region_t region, void *out[], int max);

/**
 *  Firmware-side randomized fragmentation stress for one region:
 *  repeatedly allocate a random-size block (or free a random live one) for
 *  `iters` iterations, then free everything and confirm the pool is fully
 *  recovered (no leaked blocks, no corruption).  Returns 0 on PASS, or a
 *  non-zero failure code (1=bad region, 2=corruption mid-run, 3=corruption
 *  after sweep, 4=not fully recovered).  `seed` makes the run reproducible.
 */
int sram_stress_test(sram_region_t region, uint32_t iters, uint32_t seed);

#endif /* __SRAM_POOL_H */
