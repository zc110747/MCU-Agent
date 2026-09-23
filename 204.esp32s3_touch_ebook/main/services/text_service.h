/**
 * @file text_service.h
 * @brief Turning a file on the SD card into something LVGL can draw.
 *
 * THE PROBLEM
 * -----------
 * LVGL renders UTF-8 and nothing else.  A Chinese .txt file produced on a
 * Windows machine is overwhelmingly likely to be GBK (code page 936), because
 * that is what Notepad has always written by default.  Handing those bytes to
 * LVGL produces a row of replacement boxes - a failure that looks like a font
 * problem and is actually an encoding problem, which is exactly the kind of
 * misdiagnosis worth spending 48 KB of flash to avoid.
 *
 * So every file goes through here first: detect the encoding, convert to UTF-8,
 * normalise line endings, and report what was lost.  Pages then deal with one
 * representation and one font.
 *
 * WHAT IS DELIBERATELY NOT SUPPORTED
 * ----------------------------------
 * GB18030 four-byte sequences (rare characters, and never in the text files
 * people actually keep), UTF-16, and Big5.  Each of those is a table of its own
 * and none of them is worth carrying for a first release.  They are detected
 * and reported as Unknown rather than silently mangled, so the page can say
 * why it refuses the file.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

namespace services {

/** File size ceiling: above this the page shows an error instead of reading it. */
constexpr size_t kTextMaxBytes = 256 * 1024;

enum class TextEncoding : uint8_t {
    Utf8,     /* UTF-8, no BOM - a few undecodable bytes are tolerated */
    Utf8Bom,  /* UTF-8 with a leading EF BB BF                          */
    Gbk,      /* code page 936 - converted on load                      */
    Unknown,  /* neither of the above: refuse, do not guess             */
};

const char *text_encoding_name(TextEncoding enc);

/**
 * @brief Detect the encoding of a whole buffer (it needs the whole buffer).
 *
 * Utf8 covers a file that is *predominantly* valid UTF-8 rather than only one
 * that validates byte for byte, and the difference is not academic.  The
 * fallback here is not "give up", it is "decode as GBK" - and a GBK pair almost
 * always looks like a valid UTF-8 sequence, so a single stray byte in a UTF-8
 * document used to hand the entire file to the GBK decoder, turning every
 * Chinese character in it into a different one.  A file that is overwhelmingly
 * valid UTF-8 and contains real multi-byte sequences is therefore treated as
 * UTF-8, and the bytes that really are undecodable become U+FFFD in
 * text_to_utf8() - a few replacement boxes instead of a page of nonsense.
 */
TextEncoding text_detect(const void *raw, size_t len);

/**
 * @brief Convert a whole buffer to a fresh UTF-8 buffer.
 *
 * Line endings are normalised (CRLF and lone CR become LF) and tabs become four
 * spaces, so that the Reader's pagination and LVGL's line breaking see one
 * convention.  Bytes that cannot be mapped become U+FFFD and are counted in
 * @p lost - a truncated file is then visible in the UI rather than mysterious.
 *
 * @return a heap buffer the caller frees, or NULL on out-of-memory.
 */
char *text_to_utf8(const void *raw, size_t len, TextEncoding enc,
                   size_t *out_len, uint32_t *lost);

/** @brief The result of loading a file. */
struct TextDoc {
    char        *utf8 = nullptr;      /* NUL terminated, owned by this struct  */
    size_t       len = 0;
    uint32_t     lost = 0;            /* unmappable bytes                      */
    uint32_t     lines = 0;           /* '\n' count + 1                        */
    uint32_t     bytes_on_disk = 0;
    TextEncoding encoding = TextEncoding::Unknown;
};

/**
 * @brief Read a whole file and convert it.
 *
 * Returns ESP_ERR_NOT_FOUND when the file is missing, ESP_ERR_INVALID_SIZE when
 * it is too large to be worth reading, and ESP_FAIL when the encoding is
 * Unknown.  On success @p out owns a buffer that the caller releases with
 * text_free().
 */
esp_err_t text_load_file(const char *path, TextDoc *out);

/** @brief Release the buffer held by @p doc and reset it. */
void text_free(TextDoc *doc);

/**
 * @brief Encoding name plus the size, e.g. "UTF-8  1.4 KB".
 *
 * Every page that displays a text file wants this line, and they must all say
 * it the same way, so it lives here rather than being rebuilt per page.
 */
void text_describe(const TextDoc *doc, char *dst, size_t len);

}  // namespace services
