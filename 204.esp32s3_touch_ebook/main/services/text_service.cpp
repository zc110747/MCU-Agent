/**
 * @file text_service.cpp
 * @brief Encoding detection and conversion to UTF-8.
 */

#include "text_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gbk_table.h"
#include "storage_service.h"

#include "esp_log.h"

static const char *TAG = "text";

namespace services {

namespace {

constexpr uint32_t kReplacementChar = 0xFFFD;

/* ---------------------------------------------------------------------- */
/* UTF-8                                                                    */
/* ---------------------------------------------------------------------- */

/**
 * @brief Length of the UTF-8 sequence starting at @p s, or 0 when invalid.
 *
 * Checks the continuation bytes and the overlong / surrogate / >U+10FFFF
 * ranges.  A permissive check (looking only at the lead byte) would accept a
 * byte-mangled file as UTF-8 and then the Reader would show replacement boxes
 * with no indication of why.
 */
size_t utf8_sequence_len(const uint8_t *s, size_t avail)
{
    if (avail == 0) {
        return 0;
    }
    const uint8_t c = s[0];
    if (c < 0x80) {
        return 1;
    }

    size_t len;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) {
        len = 2;
        cp = c & 0x1Fu;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
        cp = c & 0x0Fu;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
        cp = c & 0x07u;
    } else {
        return 0;
    }
    if (avail < len) {
        return 0;
    }
    for (size_t i = 1; i < len; ++i) {
        if ((s[i] & 0xC0) != 0x80) {
            return 0;
        }
        cp = (cp << 6) | (uint32_t)(s[i] & 0x3Fu);
    }

    /* Overlong encodings, surrogates and out-of-range code points are all
     * "valid UTF-8 bytes, invalid UTF-8 text".                          */
    static const uint32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < kMin[len] || cp > 0x10FFFF) {
        return 0;
    }
    if (cp >= 0xD800 && cp <= 0xDFFF) {
        return 0;
    }
    return len;
}

/**
 * @brief What a pass over a buffer as UTF-8 found.
 *
 * Reported as counts rather than a yes/no because the case that matters is
 * neither.  A UTF-8 file containing one stray byte - a smart quote pasted in
 * from Word, a download cut off mid-character - is still a UTF-8 file, and
 * calling it "not UTF-8" is exactly how a whole document ends up decoded as
 * GBK, i.e. displayed as Chinese that is wrong one character at a time.
 */
struct Utf8Scan {
    size_t sequences = 0;   /* well-formed sequences                        */
    size_t multibyte = 0;   /* of those, the ones that are not plain ASCII  */
    size_t bad       = 0;   /* bytes belonging to no valid sequence         */
};

Utf8Scan utf8_scan(const uint8_t *s, size_t len)
{
    Utf8Scan out;
    size_t i = 0;
    while (i < len) {
        const size_t n = utf8_sequence_len(s + i, len - i);
        if (n == 0) {
            ++out.bad;
            ++i;
            continue;
        }
        ++out.sequences;
        if (n > 1) {
            ++out.multibyte;
        }
        i += n;
    }
    return out;
}

/**
 * @brief How much of a file may be unreadable before it stops counting as
 *        UTF-8, as a percentage of the file's length.
 *
 * Three rather than nought because the alternative is not "reject the file" but
 * "reinterpret the file", and that is a far bigger change than losing three
 * bytes in a hundred.
 */
constexpr size_t kUtf8BadPercent = 3;

/* ---------------------------------------------------------------------- */
/* GBK                                                                      */
/* ---------------------------------------------------------------------- */

/** @brief Unicode code point for a GBK pair, or 0 when CP936 has no such pair. */
uint16_t gbk_lookup(uint8_t lead, uint8_t trail)
{
    if (lead < GBK_LEAD_MIN || lead > GBK_LEAD_MAX) {
        return 0;
    }
    if (trail < GBK_TRAIL_MIN || trail > GBK_TRAIL_MAX) {
        return 0;
    }
    return gbk_unicode_table[(size_t)(lead - GBK_LEAD_MIN) * GBK_TRAIL_COUNT +
                             (size_t)(trail - GBK_TRAIL_MIN)];
}

/**
 * @brief How much of @p s decodes cleanly as GBK, in percent.
 *
 * Only used to decide between "this is GBK" and "this is something we do not
 * speak".  The threshold is deliberately high: misreading a binary file as GBK
 * would fill the reader with plausible-looking Chinese, and that looks like a
 * success.  Refusing to open a file is a far cheaper mistake.
 */
int gbk_coverage(const uint8_t *s, size_t len)
{
    if (len == 0) {
        return 100;
    }
    size_t i = 0;
    size_t decoded = 0;

    while (i < len) {
        const uint8_t c = s[i];
        if (c < 0x80 || c == GBK_SINGLE_EURO) {
            ++i;
            ++decoded;
        } else if (i + 1 < len && gbk_lookup(c, s[i + 1]) != 0) {
            i += 2;
            decoded += 2;
        } else {
            ++i;   /* unrepresentable byte: skipped, not counted */
        }
    }
    return (int)((decoded * 100u) / len);
}

/* ---------------------------------------------------------------------- */
/* conversion                                                               */
/* ---------------------------------------------------------------------- */

size_t utf8_encode(uint32_t cp, char *dst)
{
    if (cp < 0x80) {
        dst[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    dst[0] = (char)(0xF0 | (cp >> 18));
    dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    dst[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/**
 * @brief Output sink that normalises layout as it writes.
 *
 * Handling CR / LF / TAB here, in the byte loop, is safe even for UTF-8 input:
 * every byte this cares about is below 0x80, and no byte of a multi-byte UTF-8
 * sequence is below 0x80.  That is what lets one pass do both the decode and
 * the normalisation, and why there is no second buffer.
 */
struct Writer {
    char    *buf;
    size_t   cap;
    size_t   len = 0;
    uint32_t overflow = 0;   /* bytes dropped because the buffer was full */

    void put(char c)
    {
        if (len + 1 >= cap) {
            ++overflow;
            return;
        }
        buf[len++] = c;
    }

    void put_repeat(char c, int n)
    {
        for (int i = 0; i < n; ++i) {
            put(c);
        }
    }

    void put_cp(uint32_t cp)
    {
        char tmp[4];
        const size_t n = utf8_encode(cp, tmp);
        if (len + n >= cap) {
            ++overflow;
            return;
        }
        memcpy(buf + len, tmp, n);
        len += n;
    }

    /**
     * @brief Apply the layout rule for the byte at s[i].
     *
     * @return how many source bytes this consumed: 0 when the byte is content
     *         and the caller should decode it, 1 for a plain layout byte, and 2
     *         for CRLF (both bytes are one line break).
     */
    size_t handle_layout(const uint8_t *s, size_t i, size_t len)
    {
        const unsigned char c = s[i];
        if (c == '\r') {
            /* CR and CRLF both become a single LF; a CR not followed by LF is
             * an old Mac line ending and means the same thing. */
            put('\n');
            return (i + 1 < len && s[i + 1] == '\n') ? 2 : 1;
        }
        if (c == '\t') {
            put_repeat(' ', 4);
            return 1;
        }
        if (c < 0x20 && c != '\n') {
            /* No glyph; would draw as a box. */
            return 1;
        }
        return 0;
    }

    void finish()
    {
        if (len < cap) {
            buf[len] = '\0';
        } else if (cap > 0) {
            buf[cap - 1] = '\0';
            len = cap - 1;
        }
    }
};

}  // namespace

/* ------------------------------------------------------------------------ */
/* public API                                                                */
/* ------------------------------------------------------------------------ */

const char *text_encoding_name(TextEncoding enc)
{
    switch (enc) {
    case TextEncoding::Utf8:    return "UTF-8";
    case TextEncoding::Utf8Bom: return "UTF-8 BOM";
    case TextEncoding::Gbk:     return "GBK/936";
    default:                    return "unknown";
    }
}

TextEncoding text_detect(const void *raw, size_t len)
{
    if (raw == nullptr || len == 0) {
        return TextEncoding::Utf8;   /* empty file: nothing to get wrong */
    }
    const uint8_t *s = static_cast<const uint8_t *>(raw);

    if (len >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF) {
        return TextEncoding::Utf8Bom;
    }

    const Utf8Scan u = utf8_scan(s, len);
    if (u.bad == 0) {
        return TextEncoding::Utf8;
    }

    /* Mostly valid UTF-8, and carrying real multi-byte sequences: a UTF-8 file
     * with a few damaged bytes, not a GBK file.
     *
     * Both tests are needed and neither is sufficient.  The tolerance alone
     * would misread an ASCII file with a couple of GBK characters in it - all
     * that ASCII stays well inside 3 %, so those two characters would decide
     * the encoding on their own.  The multi-byte count alone would accept a
     * UTF-8 file whose Chinese had been damaged into single bytes.  Together
     * they are a fingerprint: thousands of valid three-byte sequences cannot
     * come from a GBK file, because a GBK pair almost never forms one.
     *
     * The asymmetry is what makes 3 % the right trade.  A GBK file misread as
     * UTF-8 loses a handful of characters and says so; a UTF-8 file misread as
     * GBK has every Chinese character in it turned into a different one, which
     * is precisely "the Chinese is garbled and the English is fine". */
    if (u.multibyte > 0 && u.bad * 100 <= len * kUtf8BadPercent) {
        return TextEncoding::Utf8;
    }

    /* Checked after the UTF-8 claims above, because "valid UTF-8" is a far
     * stronger statement than "GBK coverage is high": a real GBK file cannot
     * also pass the UTF-8 validator, since that would need every dual-byte
     * character to be a well-formed sequence. */
    if (gbk_coverage(s, len) >= 95) {
        return TextEncoding::Gbk;
    }
    return TextEncoding::Unknown;
}

char *text_to_utf8(const void *raw, size_t len, TextEncoding enc, size_t *out_len, uint32_t *lost)
{
    if (out_len != nullptr) {
        *out_len = 0;
    }
    if (lost != nullptr) {
        *lost = 0;
    }
    if (raw == nullptr || len == 0) {
        char *empty = static_cast<char *>(malloc(1));
        if (empty != nullptr) {
            empty[0] = '\0';
        }
        return empty;
    }

    const uint8_t *s = static_cast<const uint8_t *>(raw);

    /* One byte in becomes at most three: a GBK pair (2 bytes) expands to at
     * most 3 UTF-8 bytes, and an unmappable byte becomes a 3-byte U+FFFD.  The
     * only thing that can exceed that is a tab (1 byte -> 4 spaces), which the
     * Writer clamps and counts if it ever runs out of room. */
    const size_t cap = len * 3 + 8;
    char *out = static_cast<char *>(malloc(cap));
    if (out == nullptr) {
        ESP_LOGE(TAG, "cannot allocate %u bytes for text conversion", (unsigned)cap);
        return nullptr;
    }

    Writer w{out, cap};
    uint32_t dropped = 0;

    if (enc == TextEncoding::Utf8 || enc == TextEncoding::Utf8Bom) {
        size_t i = (enc == TextEncoding::Utf8Bom) ? 3 : 0;
        while (i < len) {
            const size_t consumed = w.handle_layout(s, i, len);
            if (consumed != 0) {
                i += consumed;
                continue;
            }
            /* Validated, not copied byte by byte.  text_detect() accepts a file
             * that is only *mostly* UTF-8, so whatever is left here is exactly
             * the bytes nothing can decode - and passing those through would
             * have LVGL invent a codepoint out of them.  A replacement box is
             * the honest rendering of a byte that means nothing. */
            const size_t n = utf8_sequence_len(s + i, len - i);
            if (n == 0) {
                w.put_cp(kReplacementChar);
                ++dropped;
                ++i;
                continue;
            }
            for (size_t k = 0; k < n; ++k) {
                w.put((char)s[i + k]);
            }
            i += n;
        }
    } else if (enc == TextEncoding::Gbk) {
        size_t i = 0;
        while (i < len) {
            const size_t consumed = w.handle_layout(s, i, len);
            if (consumed != 0) {
                i += consumed;
                continue;
            }
            const unsigned char c = s[i];
            if (c < 0x80) {
                w.put((char)c);
                ++i;
            } else if (c == GBK_SINGLE_EURO) {
                w.put_cp(0x20AC);
                ++i;
            } else if (i + 1 < len && gbk_lookup(c, s[i + 1]) != 0) {
                w.put_cp(gbk_lookup(c, s[i + 1]));
                i += 2;
            } else {
                w.put_cp(kReplacementChar);
                ++i;
                ++dropped;
            }
        }
    } else {
        /* Unknown: the bytes are handed back unchanged rather than guessed at.
         * The caller already knows the encoding, so it can refuse to render. */
        for (size_t i = 0; i < len; ++i) {
            w.put((char)s[i]);
        }
    }

    w.finish();

    if (w.overflow > 0) {
        ESP_LOGW(TAG, "text buffer full: %u bytes dropped", (unsigned)w.overflow);
        dropped += w.overflow;
    }

    if (out_len != nullptr) {
        *out_len = w.len;
    }
    if (lost != nullptr) {
        *lost = dropped;
    }
    return out;
}

esp_err_t text_load_file(const char *path, TextDoc *out)
{
    if (path == nullptr || out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = TextDoc{};

    const uint32_t size = storage_file_size(path);
    if (size == 0) {
        /* Zero means either "missing" or "genuinely empty"; the page's next
         * step is the same either way, so they are reported together. */
        return storage_exists(path) ? ESP_ERR_INVALID_SIZE : ESP_ERR_NOT_FOUND;
    }
    if (size > kTextMaxBytes) {
        ESP_LOGW(TAG, "%s is %u bytes, over the %u byte ceiling",
                 path, (unsigned)size, (unsigned)kTextMaxBytes);
        return ESP_ERR_INVALID_SIZE;
    }

    char *raw = static_cast<char *>(malloc(size + 1));
    if (raw == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    size_t got = 0;
    esp_err_t err = storage_read(path, raw, size, &got);
    if (err != ESP_OK) {
        free(raw);
        return err;
    }
    raw[got] = '\0';

    const TextEncoding enc = text_detect(raw, got);
    if (enc == TextEncoding::Unknown) {
        ESP_LOGW(TAG, "%s: not UTF-8 and not usable GBK (%u bytes)", path, (unsigned)got);
        free(raw);
        out->encoding = enc;
        out->bytes_on_disk = (uint32_t)got;
        return ESP_FAIL;
    }

    size_t utf8_len = 0;
    uint32_t lost = 0;
    char *utf8 = text_to_utf8(raw, got, enc, &utf8_len, &lost);
    free(raw);
    if (utf8 == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    uint32_t lines = 1;
    for (size_t i = 0; i < utf8_len; ++i) {
        if (utf8[i] == '\n') {
            ++lines;
        }
    }

    out->utf8 = utf8;
    out->len = utf8_len;
    out->encoding = enc;
    out->lost = lost;
    out->lines = lines;
    out->bytes_on_disk = (uint32_t)got;

    ESP_LOGI(TAG, "%s: %s, %u bytes -> %u bytes UTF-8, %u lines, %u unmappable",
             path, text_encoding_name(enc), (unsigned)got, (unsigned)utf8_len,
             (unsigned)lines, (unsigned)lost);
    return ESP_OK;
}

void text_free(TextDoc *doc)
{
    if (doc == nullptr) {
        return;
    }
    free(doc->utf8);
    *doc = TextDoc{};
}

void text_describe(const TextDoc *doc, char *dst, size_t len)
{
    if (doc == nullptr || dst == nullptr || len == 0) {
        return;
    }
    char size[24];
    storage_human_size(size, sizeof(size), doc->bytes_on_disk);
    if (doc->lost > 0) {
        snprintf(dst, len, "%s  %s  %u damaged", text_encoding_name(doc->encoding),
                 size, (unsigned)doc->lost);
    } else {
        snprintf(dst, len, "%s  %s  %u lines", text_encoding_name(doc->encoding),
                 size, (unsigned)doc->lines);
    }
}

}  // namespace services
