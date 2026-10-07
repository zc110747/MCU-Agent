/**
 * @file nes_port.h
 * @brief NES core port interface: memory, file I/O, timing, blit.
 *
 * The upstream NES core (third_party/nes/) calls exactly the functions
 * declared here.  Everything platform-specific is implemented in nes_port.c
 * against this project's BSP (bsp_lcd / bsp_gpu / RT-Thread).
 *
 * Derived from the upstream RA8D1 vision-board NES project
 * (projects/nes/vision_board_mipi_2.0inch_nes, MIT, (c) 2023 Dozingfiretruck).
 */
#ifndef _NES_PORT_
#define _NES_PORT_

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nes_conf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* log - routed through RT-Thread's console (bsp_uart.c -> SCI9). */
#ifdef __DEBUG__
#define nes_printf(...)  rt_kprintf(__VA_ARGS__)
#else
#define nes_printf(...)
#endif

/* memory */
void *nes_malloc(int num);
void nes_free(void *address);
void *nes_memcpy(void *str1, const void *str2, size_t n);
void *nes_memset(void *str, int c, size_t n);
int nes_memcmp(const void *str1, const void *str2, size_t n);

#if (NES_USE_FS == 1)
/* io - thin wrappers over the C library, which bsp_syscalls.c maps onto the
   RT-Thread DFS (elm FatFs on the W25Q64). */
FILE *nes_fopen(const char *filename, const char *mode);
size_t nes_fread(void *ptr, size_t size_of_elements, size_t number_of_elements, FILE *a_file);
int nes_fseek(FILE *stream, long int offset, int whence);
int nes_fclose(FILE *fp);
#endif

/* Timing: called once per emulated frame (60 Hz). */
void nes_wait(uint32_t ms);

/* Present an NES frame region.  `color_data` is RGB565, `(x2-x1+1)` wide. */
int nes_draw(size_t x1, size_t y1, size_t x2, size_t y2, nes_color_t *color_data);

/* Emulator lifecycle hooks the core calls around a session.  Implemented in
   nes_port.c; the input device is attached here.  The core's own declarations
   (nes.h) are the authority - these are declared there, not here. */

/* Observability for the acceptance scripts: how many frame regions were
   presented, how many were refused, and the size of the last one. */
uint32_t nes_port_draw_calls(void);
uint32_t nes_port_draw_failed(void);
void     nes_port_last_draw_size(uint32_t *w, uint32_t *h);

/* Present-path cost.  The port renders a 240x240 surface as a single DRW2D
   submission, so "how long is one frame's presentation" is a real 60 Hz risk
   rather than a rounding error.  Needs DWT->CYCCNT, enabled in main(). */
void     nes_port_draw_timing_reset(void);
uint32_t nes_port_draw_cycles_per_frame(void);
uint32_t nes_port_draw_frames(void);

/* Whole-frame cost, sampled inside nes_frame() (the core's per-frame hook).
   Includes the pacing sleep, so it is the frame *period*, not the work. */
uint32_t nes_port_frame_cycles(void);
uint32_t nes_port_frame_samples(void);

/* Emulation slice only: the time the core spent producing one frame, excluding
   the pacing sleep.  This is the figure to compare against the 8,000,000-cycle
   60 Hz budget -- if it exceeds that, the emulator itself is the limiter and no
   scheduling change can fix the frame rate.
   nes_port_emu_timing_reset() clears every timing figure above and below. */
void     nes_port_emu_timing_reset(void);
uint32_t nes_port_emu_cycles_per_frame(void);
uint32_t nes_port_emu_max_cycles(void);

/* Duration of the most recent pacing sleep.  Nominally the 16 ms the port asks
   for; a much larger number means the scheduler starved the thread rather than
   the emulator being slow, which is a different fix. */
uint32_t nes_port_sleep_cycles(void);

/* Frame-to-frame period in CPU cycles (last sample and worst case since the
   last timing reset).  A correct 60 Hz emulator shows ~8,000,000 here; a much
   larger figure means the thread is being starved, not that it computes slowly. */
uint32_t nes_port_frame_period_cycles(void);
uint32_t nes_port_frame_period_max_cycles(void);

/* Port-layer self test: render a synthetic frame with no ROM involved. */
void nes_port_run_test_pattern(void);

/* GLCDC frame-boundary notification (called from bsp_lcd.c's line ISR). */
void     nes_port_vsync_notify(void);
uint32_t nes_port_frame_count(void);

#ifdef __cplusplus
}
#endif

#endif /* _NES_PORT_ */
