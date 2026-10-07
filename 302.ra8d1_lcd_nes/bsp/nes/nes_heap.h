/**
 * @file nes_heap.h
 * @brief Dedicated SDRAM allocator for the NES core (see nes_heap.c).
 */
#ifndef _NES_HEAP_
#define _NES_HEAP_

#include <rtthread.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Formats the linker-provided pool into a single free block.  Idempotent;
   calling it again discards whatever was allocated before.  Returns nothing -
   inspect nes_heap_ready() afterwards. */
void nes_heap_init(void);

/* True once nes_heap_init() has found a usable pool. */
rt_bool_t nes_heap_ready(void);

/* First-fit allocation, 8-byte aligned.  NULL when the pool cannot satisfy
   the request (also counted in nes_heap_fail_count()). */
void *nes_heap_alloc(uint32_t size);

/* Returns a block to the pool.  Adjacent free blocks are coalesced.  Passing a
   pointer that this pool never handed out, or freeing twice, is logged and
   ignored rather than corrupting the free list. */
void nes_heap_free(void *ptr);

/* Any argument may be NULL.  `peak` is the high-water mark since init. */
void nes_heap_info(uint32_t *total, uint32_t *used, uint32_t *peak,
                   uint32_t *nalloc, uint32_t *nfail);

uint32_t nes_heap_peak(void);
uint32_t nes_heap_fail_count(void);

/* ------------------------------------------------------------------ state */

/* On-chip SRAM arena for the emulator's *hot* structure.
 *
 * nes_t contains nes_cpu.cpu_ram (2 KB) and the PPU registers, all of which the
 * interpreter touches on every opcode and every scanline.  Measured with nes_t
 * in SDRAM, one frame of emulation took 18.2 M cycles against an 8 M budget
 * (227%); the fix is to keep the hot structure on-chip and leave the ROM images
 * -- read once, then streamed -- in SDRAM.
 *
 * The upstream core puts its singleton in a section called .NESccm for exactly
 * this reason.  This project cannot fork third_party/nes, so the equivalent is
 * done by routing the allocation here instead.
 */
void      nes_state_pool_init(void);
rt_bool_t nes_state_pool_ready(void);

/* Bytes available in the on-chip arena. */
uint32_t  nes_state_pool_size(void);

/* True when `p` lies inside the on-chip arena (used to decide whether a free
   must go back to this pool or to the SDRAM one). */
rt_bool_t nes_state_pool_owns(const void *p);

/* Address of the first slot, i.e. the handle the arena hands out first.  Used
   to reclaim the singleton before re-allocating it; see nes_malloc(). */
void *    nes_state_pool_base(void);

/* Arena allocator.  Only one object is ever placed here (nes_t), so this is a
   single-cursor bump allocator with per-object free. */
void *    nes_state_pool_alloc(uint32_t size);
void      nes_state_pool_free(void *p);

/* ------------------------------------------------------------------ staging */

/* The ROM staging window, in ON-CHIP SRAM (linker section .nes_rom).
 *
 * The filesystem that will supply ROMs in normal operation does not exist until
 * Phase 3, so until then a host-side script writes a cartridge image here over
 * SWD and `nes runmem` loads it through the core's in-memory entry point.
 *
 * On-chip rather than in the SDRAM pool because nes_load_rom() does not copy the
 * image: its PRG/CHR bank pointers alias this buffer, and the CPU fetches every
 * opcode through them.  In SDRAM that put the instruction stream on the slow bus
 * (41% of emulation time measured in nes_read_cpu).
 *
 * Call nes_heap_staging_reserve() once after nes_heap_init(); the address is
 * stable for the life of the program. */
void      nes_heap_staging_reserve(void);

/* Address and size of the staging window, or NULL/0 when it could not be
   reserved.  The address is stable for the lifetime of the pool. */
uint8_t * nes_heap_staging(uint32_t *size);
uint32_t  nes_heap_staging_size(void);

#ifdef __cplusplus
}
#endif

#endif /* _NES_HEAP_ */
