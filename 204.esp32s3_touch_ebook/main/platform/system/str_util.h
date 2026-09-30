/**
 * @file str_util.h
 * @brief Bounded string copying that says what it means.
 *
 * WHY THIS EXISTS
 * ---------------
 * `snprintf(dst, sizeof(dst), "%s", src)` is the obvious way to copy a string,
 * and with `-Wall -Werror` it is a build failure whenever the compiler cannot
 * bound `src`'s length: FATFS filenames are up to 255 bytes, and copying one
 * into a 128-byte field is reported as
 *
 *     '%s' directive output may be truncated writing up to 255 bytes into a
 *     region of size 128 [-Werror=format-truncation=]
 *
 * The warning is correct - the copy *can* truncate - and the intent was to
 * truncate, so the fix is to say so.  Passing an explicit precision makes the
 * bound visible to the compiler and to a reader:
 *
 *     snprintf(dst, cap, "%.*s", (int)(cap - 1), src);
 *
 * Spelling that out at every call site would be eight copies of the same
 * comment, so it lives here once.
 */
#pragma once

#include <stddef.h>
#include <stdio.h>

namespace platform {

/**
 * @brief Copy @p src into @p dst, truncating to fit rather than overflowing.
 *
 * @param dst  destination buffer
 * @param cap  its size in bytes, including the terminator
 * @param src  source; NULL writes an empty string
 */
inline void str_copy(char *dst, size_t cap, const char *src)
{
    if (dst == nullptr || cap == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    /* Guard the (int) cast: a capacity this large would overflow the precision
     * argument. Nothing in this project is anywhere near it, but the cast is
     * the kind of thing that silently breaks years later. */
    if (cap > (size_t)0x7FFFFFFF) {
        cap = (size_t)0x7FFFFFFF;
    }
    snprintf(dst, cap, "%.*s", (int)(cap - 1), src);
}

}  // namespace platform
