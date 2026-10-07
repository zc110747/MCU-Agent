/**
 * @file nes_heap.c
 * @brief Dedicated SDRAM bump/heap allocator for the NES core.
 *
 * Why not the RT-Thread heap
 * --------------------------
 * The upstream core allocates, in one emulator session:
 *   - nes_t                ~118 KB  (240x240 RGB565 nes_draw_data dominates)
 *   - PRG ROM               up to 512 KB (mapper 0/2 carts are typically 32-256 KB)
 *   - CHR ROM               up to 256 KB
 *   - cart SRAM             2 KB
 * RT-Thread's linking-time heap array (RT_HEAP_SIZE in bsp_rtthread.c) is far
 * smaller than that, and growing it would eat the on-chip SRAM that the
 * GLCDC/DRW2D paths want.  SDRAM has 128 MB and is where the framebuffers
 * already live, so the emulator's working set belongs there too.
 *
 * Why a separate heap rather than newlib malloc
 * ---------------------------------------------
 * newlib's malloc carves __HeapBase..__HeapLimit, which is on-chip SRAM.  The
 * port layer therefore redirects nes_malloc/nes_free here instead.  This keeps
 * every NES allocation in one known, bounded region - which matters because the
 * allocator is also what the W25Q64 FatFs work will eventually want to reuse.
 *
 * Algorithm: first-fit free list with in-place block splitting and coalescing
 * of adjacent free blocks.  Bounded, no recursion, no dynamic metadata, and
 * every block carries its own size - so a bad ROM cannot corrupt the pool
 * metadata of the block next to it silently.
 */
#include "nes_heap.h"

#include <rtthread.h>

#include <stdbool.h>
#include <stddef.h>

#include "nes_port.h"

/* ------------------------------------------------------------------ config */

/* Alignment for every returned pointer.  8 bytes covers uint64_t/double and
   keeps the DRW2D blit source (which is read as RGB565, i.e. 2-byte aligned)
   well satisfied. */
#define NES_HEAP_ALIGN          (8U)

/* Header prepended to every block (free and allocated alike).

   On a 32-bit target this is 12 bytes, which is not a multiple of the 8-byte
   payload alignment.  `pad` forces it to 16 so that a payload following the
   header is 8-byte aligned without any per-block rounding arithmetic. */
typedef struct nes_heap_block
{
    uint32_t               size;   /* payload bytes, excluding this header */
    uint32_t               free;   /* 1 = free, 0 = allocated */
    struct nes_heap_block *next;   /* next block in address order, NULL = end */
    uint32_t               pad;    /* keeps sizeof() a multiple of NES_HEAP_ALIGN */
} nes_heap_block_t;

_Static_assert(sizeof(nes_heap_block_t) % NES_HEAP_ALIGN == 0,
               "block header must preserve payload alignment");

/* ------------------------------------------------------------------ state */

/* Exported by linker/fsp.ld from the .nes_heap (NOLOAD) section in SDRAM. */
extern uint8_t __nes_heap_start[];
extern uint8_t __nes_heap_end[];

static nes_heap_block_t * g_head  = NULL;
static rt_bool_t          g_ready = false;

static uint32_t           g_total   = 0U;
static uint32_t           g_used    = 0U;
static uint32_t           g_peak    = 0U;
static uint32_t           g_nalloc  = 0U;
static uint32_t           g_nfail   = 0U;

/* ROM staging window (see nes_heap_staging_reserve()).  It is its own linker
   section in on-chip SRAM, not a slice of this pool - the header and size live
   here because the accessor pair below is what callers use. */

static uint8_t *          g_staging      = NULL;
static uint32_t           g_staging_size = 0U;

/* ----------------------------------------------------------------- helpers */

static uint32_t align_up (uint32_t v)
{
    return (v + (NES_HEAP_ALIGN - 1U)) & ~(NES_HEAP_ALIGN - 1U);
}

void nes_heap_init (void)
{
    uint32_t span = (uint32_t) ((uintptr_t) __nes_heap_end - (uintptr_t) __nes_heap_start);

    g_head  = NULL;
    g_ready = false;

    if (span < (sizeof(nes_heap_block_t) + NES_HEAP_ALIGN))
    {
        rt_kprintf("[nes_heap] pool too small: %u bytes\n", (unsigned int) span);
        return;
    }

    g_head        = (nes_heap_block_t *) __nes_heap_start;
    g_head->size  = span - (uint32_t) sizeof(nes_heap_block_t);
    g_head->size &= ~(NES_HEAP_ALIGN - 1U);
    g_head->free  = 1U;
    g_head->next  = NULL;

    g_total  = g_head->size;
    g_used   = 0U;
    g_peak   = 0U;
    g_nalloc = 0U;
    g_nfail  = 0U;
    g_ready  = true;

    g_staging      = NULL;
    g_staging_size = 0U;
}

rt_bool_t nes_heap_ready (void)
{
    return g_ready;
}

void * nes_heap_alloc (uint32_t size)
{
    nes_heap_block_t * blk;

    if (!g_ready || (0U == size))
    {
        return NULL;
    }

    size = align_up(size);

    for (blk = g_head; NULL != blk; blk = blk->next)
    {
        if (!blk->free || (blk->size < size))
        {
            continue;
        }

        /* Split when the remainder can hold a header plus one aligned unit;
           otherwise hand out the whole block and waste the tail. */
        if ((blk->size - size) >= (sizeof(nes_heap_block_t) + NES_HEAP_ALIGN))
        {
            nes_heap_block_t * tail =
                (nes_heap_block_t *) ((uint8_t *) blk + sizeof(nes_heap_block_t) + size);

            tail->size = blk->size - size - (uint32_t) sizeof(nes_heap_block_t);
            tail->free = 1U;
            tail->next = blk->next;

            blk->size = size;
            blk->next = tail;
        }

        blk->free = 0U;
        g_used  += blk->size;
        g_nalloc++;

        if (g_used > g_peak)
        {
            g_peak = g_used;
        }

        return (void *) ((uint8_t *) blk + sizeof(nes_heap_block_t));
    }

    g_nfail++;
    return NULL;
}

void nes_heap_free (void * ptr)
{
    nes_heap_block_t * blk;
    nes_heap_block_t * cur;

    if (!g_ready || (NULL == ptr))
    {
        return;
    }

    /* The caller must hand back a pointer this pool produced; a foreign
       pointer would be a porting bug, and silently ignoring it is the only
       safe response once the address is already suspect. */
    if (((uintptr_t) ptr <= (uintptr_t) __nes_heap_start) ||
        ((uintptr_t) ptr >= (uintptr_t) __nes_heap_end))
    {
        rt_kprintf("[nes_heap] free of non-pool pointer %p ignored\n", ptr);
        return;
    }

    blk = (nes_heap_block_t *) ((uint8_t *) ptr - sizeof(nes_heap_block_t));

    if (blk->free)
    {
        rt_kprintf("[nes_heap] double free at %p ignored\n", ptr);
        return;
    }

    blk->free = 1U;
    g_used = (g_used >= blk->size) ? (g_used - blk->size) : 0U;

    /* Coalesce forward chains of free blocks.  Adjacency is checked by address
       rather than by list order, so a merged run is reclaimed in one pass. */
    for (cur = g_head; NULL != cur; cur = cur->next)
    {
        if (!cur->free || (NULL == cur->next) || !cur->next->free)
        {
            continue;
        }

        if ((uint8_t *) cur + sizeof(nes_heap_block_t) + cur->size == (uint8_t *) cur->next)
        {
            cur->size += (uint32_t) sizeof(nes_heap_block_t) + cur->next->size;
            cur->next  = cur->next->next;

            /* Re-examine this block: the new neighbour may also be free. */
            cur = g_head;
        }
    }
}

void nes_heap_info (uint32_t * total, uint32_t * used, uint32_t * peak,
                    uint32_t * nalloc, uint32_t * nfail)
{
    if (NULL != total)  { *total  = g_total;  }
    if (NULL != used)   { *used   = g_used;   }
    if (NULL != peak)   { *peak   = g_peak;   }
    if (NULL != nalloc) { *nalloc = g_nalloc; }
    if (NULL != nfail)  { *nfail  = g_nfail;  }
}

uint32_t nes_heap_peak (void)
{
    return g_peak;
}

uint32_t nes_heap_fail_count (void)
{
    return g_nfail;
}

/* ------------------------------------------------------------------ state */

/* Exported by linker/fsp.ld from the .nes_state (NOLOAD) section in on-chip
   RAM.  See nes_state_pool_init() for why the emulator's hot structure does not
   live in the SDRAM pool. */
extern uint8_t __nes_state_start[];
extern uint8_t __nes_state_end[];

static uint8_t *  g_state_base  = NULL;
static uint32_t   g_state_size  = 0U;
static uint32_t   g_state_used  = 0U;

void nes_state_pool_init (void)
{
    g_state_base = __nes_state_start;
    g_state_size = (uint32_t) ((uintptr_t) __nes_state_end - (uintptr_t) __nes_state_start);
    g_state_used = 0U;
}

rt_bool_t nes_state_pool_ready (void)
{
    return (NULL != g_state_base) && (g_state_size > 0U);
}

uint32_t nes_state_pool_size (void)
{
    return g_state_size;
}

rt_bool_t nes_state_pool_owns (const void * p)
{
    uintptr_t a = (uintptr_t) p;

    return (a >= (uintptr_t) g_state_base) && (a < (uintptr_t) g_state_base + g_state_size);
}

void * nes_state_pool_base (void)
{
    return (void *) g_state_base;
}

void * nes_state_pool_alloc (uint32_t size)
{
    uint32_t aligned;

    if (!nes_state_pool_ready() || (0U == size))
    {
        return NULL;
    }

    aligned = align_up(size);

    if ((g_state_used + aligned) > g_state_size)
    {
        return NULL;
    }

    {
        uint8_t * p = g_state_base + g_state_used;

        g_state_used += aligned;
        return p;
    }
}

void nes_state_pool_free (void * p)
{
    /* Single-object arena: the pool ever hands out one pointer, the base, so any
       address inside the arena refers to that same object and reclaiming it
       simply resets the cursor.  Rejecting a foreign pointer is still worth the
       check - it means a caller confused the two arenas. */
    if (!nes_state_pool_owns(p))
    {
        rt_kprintf("[nes_heap] state free of foreign pointer %p ignored\n", p);
        return;
    }

    g_state_used = 0U;
}

/* ------------------------------------------------------------------ staging */

/* Exported by linker/fsp.ld from the .nes_rom (NOLOAD) section.
 *
 * On-chip on purpose.  nes_load_rom() does not copy the cartridge image - the
 * PRG/CHR bank pointers alias whatever buffer it is handed - and the NES CPU
 * fetches every opcode byte through those pointers.  With the image in SDRAM the
 * instruction stream rode the slow bus: PC sampling put 41% of emulation time in
 * nes_read_cpu (which is what indexes prg_banks[]) and 28% in the opcode
 * dispatcher, while the PPU renderers were never sampled at all.  Staging the
 * image in on-chip SRAM removes the bus cost without touching the core. */
extern uint8_t __nes_rom_start[];
extern uint8_t __nes_rom_end[];

void nes_heap_staging_reserve (void)
{
    uint32_t span = (uint32_t) ((uintptr_t) __nes_rom_end - (uintptr_t) __nes_rom_start);

    if (span < 1024U)
    {
        g_staging      = NULL;
        g_staging_size = 0U;
        return;
    }

    g_staging      = __nes_rom_start;
    g_staging_size = span;
}

uint8_t * nes_heap_staging (uint32_t * size)
{
    if (NULL != size)
    {
        *size = g_staging_size;
    }

    return g_staging;
}

uint32_t nes_heap_staging_size (void)
{
    return g_staging_size;
}
