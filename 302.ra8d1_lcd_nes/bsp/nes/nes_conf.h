/**
 * @file nes_conf.h
 * @brief NES core build-time configuration (port layer).
 *
 * Derived from the upstream RA8D1 vision-board NES project
 * (projects/nes/vision_board_mipi_2.0inch_nes, MIT, (c) 2023 Dozingfiretruck),
 * retargeted to this project's BSP.  The upstream core in third_party/nes/ is
 * kept verbatim; every knob it needs lives here.
 */
#ifndef _NES_CONF_
#define _NES_CONF_

#ifdef __cplusplus
extern "C" {
#endif

/* Render every frame; the pipeline is fast enough and skipping makes the
   emulator seen to "jump" in a way that hides real frame-rate regressions. */
#define NES_FRAME_SKIP          0

/* Screenshot buffer: 240x240 NES output, RGB565. */
#define NES_USE_SRAM            1
#define NES_COLOR_DEPTH         16
#define NES_COLOR_SWAP          0
#define NES_RAM_LACK            0
#define NES_USE_FS              1

#ifndef NES_USE_FS
#define NES_USE_FS              0
#endif

#ifndef NES_FRAME_SKIP
#define NES_FRAME_SKIP          0
#endif

#ifndef NES_RAM_LACK
#define NES_RAM_LACK            0
#endif

#if (NES_RAM_LACK == 1)
#define NES_DRAW_SIZE         (NES_WIDTH * NES_HEIGHT / 2)
#else
#define NES_DRAW_SIZE         (NES_WIDTH * NES_HEIGHT)
#endif

#ifndef NES_COLOR_SWAP
#define NES_COLOR_SWAP          0
#endif

/* Color depth:
 * - 16: RGB565   (what the panel takes)
 * - 32: ARGB8888
 */
#ifndef NES_COLOR_DEPTH
#define NES_COLOR_DEPTH         16
#endif

#if (NES_COLOR_DEPTH == 32)
#define nes_color_t uint32_t
#elif (NES_COLOR_DEPTH == 16)
#define nes_color_t uint16_t
#else
#error "no supprt color depth"
#endif

#define M_PI 3.14159265358979323846

#ifdef __cplusplus
}
#endif

#endif /* _NES_CONF_ */
