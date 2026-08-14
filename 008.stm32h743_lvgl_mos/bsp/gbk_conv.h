/*
 * bsp/gbk_conv.h
 * Minimal GBK(CP936) -> UTF-8 transcoder for the serial console.
 *
 * The SD card 8.3 short names are stored in the volume OEM codepage (GBK on a
 * Chinese-formatted card). They are kept as GBK inside the firmware (so that
 * f_open() matches the on-disk bytes), and only transcoded to UTF-8 right
 * before being printed to the serial console, which expects UTF-8.
 */

#ifndef GBK_CONV_H
#define GBK_CONV_H

#include <stddef.h>
#include <stdint.h>

/*
 * Convert a NUL-terminated GBK string to UTF-8.
 *   gbk      : source string (may contain ASCII and GBK double-byte sequences)
 *   out      : destination buffer
 *   out_size : size of out in bytes (must be > 0)
 * Returns the number of bytes written (excluding the terminating NUL).
 * Unmappable sequences are replaced with '?'. out is always NUL-terminated
 * if out_size > 0.
 */
int gbk_to_utf8(const char *gbk, char *out, int out_size);

/*
 * Test whether buf[0..len) is well-formed UTF-8.
 *   buf : bytes to test (not required to be NUL-terminated)
 *   len : number of bytes
 * Returns 1 if the buffer is valid UTF-8, 0 otherwise.
 *
 * A trailing multi-byte sequence that is cut off by len (i.e. the file was
 * read in a truncated chunk) is tolerated so callers can use this on a
 * partially-loaded file; any in-sequence invalid byte or overlong encoding
 * fails the test.  Pure ASCII is, of course, valid UTF-8.
 *
 * This is the discriminator used by the TXT reader to decide between UTF-8
 * (no BOM) and GBK source text.
 */
int utf8_is_valid(const uint8_t *buf, uint32_t len);

#endif /* GBK_CONV_H */
