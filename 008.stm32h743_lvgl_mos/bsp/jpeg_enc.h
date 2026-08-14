#ifndef JPEG_ENC_H
#define JPEG_ENC_H

#include <stdint.h>

/*
 * Minimal baseline JPEG (4:4:4, 8-bit) encoder for RGB565 framebuffers.
 *
 * Uses the standard JPEG Annex-K Huffman tables and standard luminance /
 * chrominance quantization tables (scaled by a fixed quality), so the output
 * is decodable by every conformant JPEG reader.  No dynamic allocation, no
 * frequency-statistics — robust and tiny enough for an STM32H743 screenshot.
 *
 * Input : w*h RGB565 pixels (little-endian 16-bit, as on the ST7789 framebuffer)
 * Output: JPEG byte stream (caller supplies the buffer)
 *
 * Returns the number of bytes written, or -1 on error (NULL args / overflow).
 */
int jpeg_encode_rgb565(const uint16_t *rgb565, int w, int h,
                        uint8_t *out, int out_capacity);

#endif /* JPEG_ENC_H */
