/**
 * @file nes_port.c
 * @brief NES core port layer for the RA8D1 vision board.
 *
 * The upstream NES core (third_party/nes/) is emulator logic only; everything
 * that touches the board lives here:
 *   memory    -> dedicated SDRAM pool (nes_heap.c), see note below
 *   file I/O  -> libc fopen/fread, mapped onto the DFS by bsp_syscalls.c
 *   timing    -> RT-Thread tick (60 Hz per emulated frame)
 *   present   -> DRW2D blit into the GLCDC framebuffer (bsp_gpu.c)
 *   input     -> serial "virtual gamepad" (rt_key_scan.c)
 *
 * Derived from the upstream RA8D1 vision-board NES project
 * (projects/nes/vision_board_mipi_2.0inch_nes, MIT, (c) 2023 Dozingfiretruck),
 * which drove the panel through lcd_gpu_fill_array().  The DRW2D call chain is
 * preserved; the frame buffer and the panel bring-up are this project's BSP.
 *
 * Memory note
 * -----------
 * nes_malloc() does NOT use the RT-Thread heap.  nes_t alone is ~118 KB (the
 * 240x240 RGB565 nes_draw_data array dominates it) and the PRG/CHR images add
 * hundreds of KB on top, which is far beyond RT_HEAP_SIZE.  Allocations come
 * from bsp/nes/nes_heap.c, a bounded first-fit pool in SDRAM.
 */
#include "nes_port.h"

#include <rtthread.h>

#include "bsp_gpu.h"
#include "bsp_lcd.h"
#include "nes.h"
#include "nes_heap.h"
#include "rt_key_scan.h"

/* NES output is centred in the 480x360 panel, same geometry the DRW2D test
   pattern used in Phase 1. */
#define NES_DST_OFFSET_X    ((BSP_LCD_WIDTH  - NES_WIDTH ) / 2U)
#define NES_DST_OFFSET_Y    ((BSP_LCD_HEIGHT - NES_HEIGHT) / 2U)

/* ------------------------------------------------------------------ memory */

/*
 * Allocation routing.
 *
 * The core's first and largest request in every entry point (nes_load_rom and
 * nes_load_file) is the whole nes_t, and that structure is the hot one: its
 * nes_cpu.cpu_ram is touched by every opcode and its PPU state by every
 * scanline.  Measured with it in SDRAM, one frame of emulation cost 18.2 M
 * cycles against an 8 M budget; on-chip SRAM is markedly faster for that access
 * pattern.
 *
 * third_party/nes cannot be forked, so rather than editing the core's allocator
 * call sites this routes by request size: sizeof(nes_t) is served from the
 * on-chip arena (nes_state_pool in nes_heap.c, which is the .nes_state section
 * upstream reaches for when it asks for .NESccm), and everything else - the
 * PRG/CHR images and the cartridge SRAM, which are read once and then streamed
 * - comes from the SDRAM pool where space is abundant.
 *
 * The size test is exact rather than a threshold on purpose: a threshold would
 * silently pull an unrelated large allocation into a single-object arena.
 */
void *nes_malloc (int num)
{
    void * p;

    if (num <= 0)
    {
        return NULL;
    }

    if ((uint32_t) num == (uint32_t) sizeof(nes_t))
    {
        /* nes_t is a singleton: one emulator session at a time.  Reclaim the
           arena before handing it out again rather than relying on the core to
           free the previous instance, because it does not always do so - a
           `nes runmem` that is followed by another `nes runmem` (or a load that
           fails part way) leaves the cursor advanced, and the next request then
           fails with "arena too small" even though the arena is empty.

           Observed exactly that: the first run allocated nes_t on-chip
           (nalloc=1, heap used 73728) and the second fell back to SDRAM
           (nalloc=3, heap used 203800). */
        nes_state_pool_free(nes_state_pool_base());

        p = nes_state_pool_alloc((uint32_t) num);

        if (NULL != p)
        {
            return p;
        }

        /* Genuinely too small (a mis-sized linker section): fall through to
           SDRAM rather than failing outright, so the build degrades to "slow"
           instead of "broken". */
        rt_kprintf("[nes] on-chip state arena too small (%u < %u), using SDRAM\n",
                   (unsigned int) nes_state_pool_size(), (unsigned int) num);
    }

    return nes_heap_alloc((uint32_t) num);
}

void nes_free (void *address)
{
    /* The two arenas are disjoint, so the owning one decides where a free goes;
       handing a pointer to the wrong allocator would corrupt its metadata. */
    if (nes_state_pool_owns(address))
    {
        nes_state_pool_free(address);
        return;
    }

    nes_heap_free(address);
}

void *nes_memcpy (void *str1, const void *str2, size_t n)
{
    return rt_memcpy(str1, str2, n);
}

void *nes_memset (void *str, int c, size_t n)
{
    return rt_memset(str, c, n);
}

int nes_memcmp (const void *str1, const void *str2, size_t n)
{
    return rt_memcmp(str1, str2, n);
}

/* ---------------------------------------------------------------------- io */
#if (NES_USE_FS == 1)
FILE *nes_fopen (const char *filename, const char *mode)
{
    return fopen(filename, mode);
}

size_t nes_fread (void *ptr, size_t size_of_elements, size_t number_of_elements, FILE *a_file)
{
    return fread(ptr, size_of_elements, number_of_elements, a_file);
}

int nes_fseek (FILE *stream, long int offset, int whence)
{
    return fseek(stream, offset, whence);
}

int nes_fclose (FILE *fp)
{
    return fclose(fp);
}
#endif

/* ------------------------------------------------------------------- timing */

/*
 * Frame pacing note: nes_wait() must produce a 16.67 ms period, and RT-Thread's
 * tick is 1 kHz, so FRAMES_PER_SECOND below is 16 ms - a 4% fast frame rate
 * that is invisible in practice.  What *is* visible is when the sleep
 * overshoots, so the timestamp of the previous frame is kept and the whole
 * period (not just the sleep) is what gets reported.
 */
static volatile uint32_t g_last_frame_tick = 0U;
static volatile uint32_t g_frame_period_cyc = 0U;
static volatile uint32_t g_frame_period_max = 0U;

void nes_wait (uint32_t ms)
{
    rt_thread_mdelay(ms);
}

uint32_t nes_port_frame_period_cycles (void)
{
    return g_frame_period_cyc;
}

uint32_t nes_port_frame_period_max_cycles (void)
{
    return g_frame_period_max;
}

/* Frame boundary from the GLCDC line-detect ISR (bsp_lcd.c).  The emulator
   loop does not need to block on it today, but the hook is what a tear-free
   blit would sync to, and it makes the vsync counter observable from msh. */
static volatile uint32_t g_frame_count = 0U;

void nes_port_vsync_notify (void)
{
    g_frame_count++;
}

uint32_t nes_port_frame_count (void)
{
    return g_frame_count;
}

/* --------------------------------------------------------------------- draw */

/* Frames actually presented, and the last rectangle that was presented.  Both
   are read back by the verification script, so a "did anything reach the
   panel" question never has to be answered from the blit counter alone. */
static volatile uint32_t g_draw_calls   = 0U;
static volatile uint32_t g_draw_failed  = 0U;
static volatile uint32_t g_last_w       = 0U;
static volatile uint32_t g_last_h       = 0U;

uint32_t nes_port_draw_calls (void)
{
    return g_draw_calls;
}

uint32_t nes_port_draw_failed (void)
{
    return g_draw_failed;
}

/* Cycles spent inside nes_draw() since the last reset, and the number of
   frames measured.  The emulator's present cost is the difference between
   "runs at 60 Hz" and "runs at 60 Hz with 240 DRW2D submissions per frame", so
   it is measured rather than assumed. */
static volatile uint32_t g_draw_cycles = 0U;
static volatile uint32_t g_draw_frames = 0U;

void nes_port_draw_timing_reset (void)
{
    g_draw_cycles = 0U;
    g_draw_frames = 0U;
}

uint32_t nes_port_draw_cycles_per_frame (void)
{
    return (0U != g_draw_frames) ? (g_draw_cycles / g_draw_frames) : 0U;
}

uint32_t nes_port_draw_frames (void)
{
    return g_draw_frames;
}

void nes_port_last_draw_size (uint32_t *w, uint32_t *h)
{
    if (NULL != w) { *w = g_last_w; }
    if (NULL != h) { *h = g_last_h; }
}

/**
 * @brief Render a synthetic 240x240 frame through the real present path.
 *
 * Exists so the render path can be verified independently of ROM availability:
 * Phase 2 lands the emulator core before Phase 3 lands the filesystem the ROMs
 * live in.  The four bands are saturated, exactly-representable RGB565 values
 * so the SWD read-back script can compare with == instead of a tolerance.
 *
 * The source buffer is static here rather than in the caller so its cost
 * (240*240*2 = 112 KB) is attributable to this module, and it is deliberately
 * NOT from the NES heap: it is a test fixture, not emulator state.
 */
void nes_port_run_test_pattern (void)
{
    static uint16_t synth[NES_WIDTH * NES_HEIGHT];

    for (uint32_t y = 0U; y < NES_HEIGHT; y++)
    {
        uint16_t c;

        if (y < 60U)       { c = 0xF800U; }   /* pure red   */
        else if (y < 120U) { c = 0x07E0U; }   /* pure green */
        else if (y < 180U) { c = 0x001FU; }   /* pure blue  */
        else               { c = 0xFFFFU; }   /* white      */

        for (uint32_t x = 0U; x < NES_WIDTH; x++)
        {
            synth[(y * NES_WIDTH) + x] = c;
        }
    }

    (void) nes_draw(0U, 0U, NES_WIDTH - 1U, NES_HEIGHT - 1U, synth);
}

/**
 * @brief Present a rectangle of the emulator's frame buffer.
 *
 * The core calls this once per frame with the full 240x240 surface
 * (NES_FRAME_SKIP == 0, so nes.c:364 is the only live call site), but the
 * signature is a rectangle and the upstream mid-frame variant used it, so the
 * region is honoured rather than assumed.
 *
 * `color_data` is the base of the full 240-wide frame buffer, NOT the start of
 * the requested rectangle: nes.c passes nes->nes_draw_data and then slices with
 * (x1,y1,x2,y2).  The source pitch is therefore always NES_WIDTH.
 *
 * ONE blit for the whole rectangle.  Dave2D's source pitch is an independent
 * argument, so a sub-window of a wider surface goes out in a single submission;
 * there is no need for a staging copy.  (An earlier revision emitted one blit
 * per scanline, which measured ~10.2 M cycles per frame - 127% of the 60 Hz
 * budget.  Re-arming the engine per row is the dominant cost, not the pixels.)
 */
int nes_draw (size_t x1, size_t y1, size_t x2, size_t y2, nes_color_t *color_data)
{
    uint32_t w;
    uint32_t h;
    uint32_t t0;

    if ((NULL == color_data) || (x2 < x1) || (y2 < y1) ||
        (x2 >= NES_WIDTH) || (y2 >= NES_HEIGHT))
    {
        g_draw_failed++;
        return -1;
    }

    if (!bsp_gpu_ready())
    {
        /* The emulator runs before the engine is up only if `nes run` is issued
           from a shell that skipped main()'s bring-up; fail loud, not silent. */
        g_draw_failed++;
        return -1;
    }

    w  = (uint32_t) ((x2 - x1) + 1U);
    h  = (uint32_t) ((y2 - y1) + 1U);
    t0 = DWT->CYCCNT;

    /* Source origin is (y1 * NES_WIDTH + x1) in the 240-wide surface; the
       destination keeps the same relationship to the panel-centred window. */
    (void) bsp_gpu_blit_surface((uint32_t) NES_DST_OFFSET_X + (uint32_t) x1,
                                (uint32_t) NES_DST_OFFSET_Y + (uint32_t) y1,
                                color_data + (y1 * NES_WIDTH) + x1,
                                (uint32_t) NES_WIDTH,
                                w, h);

    g_draw_cycles += DWT->CYCCNT - t0;
    g_draw_frames++;

    g_last_w = w;
    g_last_h = h;
    g_draw_calls++;

    return 0;
}

/* -------------------------------------------------------------------- input */

int nes_initex (nes_t *nes)
{
    (void) nes;

    key_scan_init();
    return NES_OK;
}

int nes_deinitex (nes_t *nes)
{
    (void) nes;

    key_scan_capture(RT_FALSE);
    return NES_OK;
}

/* ------------------------------------------------------------------- frame */

#define FRAMES_PER_SECOND   1000/60

/* Emulation time: everything the core did between two nes_frame() calls, i.e.
   the render + opcode work for one frame, EXCLUDING the pacing sleep.  Measured
   from one nes_frame() entry to the next, before that frame's sleep, so it
   captures the whole inter-frame slice rather than just the hook body.

   This is the figure to compare against the 8,000,000-cycle 60 Hz budget: if it
   exceeds that, no scheduling change can reach 60 Hz. */
static volatile uint32_t g_emu_cycles   = 0U;
static volatile uint32_t g_emu_max      = 0U;
static volatile uint32_t g_emu_samples  = 0U;
static volatile uint32_t g_emu_prev_t   = 0U;
static volatile uint32_t g_sleep_cycles = 0U;

/* Frame period (includes the pacing sleep) and the whole nes_frame() cost. */
static volatile uint32_t g_frame_cycles = 0U;
static volatile uint32_t g_frame_count2 = 0U;

void nes_port_emu_timing_reset (void)
{
    g_emu_cycles   = 0U;
    g_emu_max      = 0U;
    g_emu_samples  = 0U;
    g_emu_prev_t   = 0U;
    g_sleep_cycles = 0U;
    g_frame_cycles = 0U;
    g_frame_count2 = 0U;
    g_last_frame_tick  = 0U;
    g_frame_period_cyc = 0U;
    g_frame_period_max = 0U;
}

uint32_t nes_port_sleep_cycles (void)
{
    return g_sleep_cycles;
}

uint32_t nes_port_frame_cycles (void)
{
    return g_frame_cycles;
}

uint32_t nes_port_frame_samples (void)
{
    return g_frame_count2;
}

uint32_t nes_port_emu_cycles_per_frame (void)
{
    return (0U != g_emu_samples) ? (g_emu_cycles / g_emu_samples) : 0U;
}

uint32_t nes_port_emu_max_cycles (void)
{
    return g_emu_max;
}

void nes_frame (nes_t *nes)
{
    uint32_t t0 = DWT->CYCCNT;
    uint32_t t1;

    /* Publish the virtual pad.  The core reads this through nes_read_joypad()
       ($4016/$4017) on the emulator thread, so writing it once per frame keeps
       input latency to at most one frame. */
    if (NULL != nes)
    {
        nes->nes_cpu.joypad.joypad = (uint16_t) key_scan_to_nes_joypad(key_scan());
    }

    /* Emulation slice: cycles burned since the previous frame's hook entry,
       minus that frame's sleep.  Everything else the emulator does for one frame
       (palette generation, 240 scanline renders, ~29780 opcode dispatches)
       happens here. */
    if (0U != g_emu_prev_t)
    {
        uint32_t slice = (t0 - g_emu_prev_t);

        slice = (slice > g_sleep_cycles) ? (slice - g_sleep_cycles) : 0U;

        g_emu_cycles += slice;
        g_emu_samples++;

        if (slice > g_emu_max)
        {
            g_emu_max = slice;
        }
    }

    /* Frame-to-frame period, including the sleep: this is what the achieved
       frame rate is actually made of. */
    if (0U != g_last_frame_tick)
    {
        uint32_t period = t0 - g_last_frame_tick;

        g_frame_period_cyc = period;

        if (period > g_frame_period_max)
        {
            g_frame_period_max = period;
        }
    }

    t1 = DWT->CYCCNT;
    nes_wait(FRAMES_PER_SECOND);
    g_sleep_cycles = DWT->CYCCNT - t1;

    g_last_frame_tick = t0;
    g_emu_prev_t      = t0;

    g_frame_cycles += DWT->CYCCNT - t0;
    g_frame_count2++;
}
