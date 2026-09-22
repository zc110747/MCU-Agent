/**
 * @file lvgl_fs.h
 * @brief Translating a POSIX path into the path LVGL's file-system layer wants.
 *
 * LVGL does not open a path; it opens a *driver letter* followed by a path.  The
 * POSIX driver enabled in sdkconfig registers the letter
 * CONFIG_LV_FS_POSIX_LETTER ('A') with an empty working directory, so
 * "/sd/photo.jpg" has to be handed over as "A:/sd/photo.jpg" and the driver
 * turns it back into exactly the absolute path ESP-IDF's VFS understands.
 *
 * This exists as one function rather than a literal repeated in each page
 * because the failure mode of getting it wrong is silent: LVGL logs "unknown
 * driver letter", draws nothing, and the page simply looks empty.
 */
#pragma once

#include <stddef.h>
#include <stdio.h>

#include "sdkconfig.h"

namespace ui {

/** @brief The drive letter LVGL's POSIX driver answers to. */
inline constexpr char kFsLetter = (char)CONFIG_LV_FS_POSIX_LETTER;

/**
 * @brief Write the LVGL form of @p posix_path into @p dst.
 *
 * @param dst  destination, at least strlen(posix_path) + 3 bytes
 * @param len  size of @p dst
 * @return false when the path did not fit, in which case @p dst is empty.
 */
inline bool lvgl_path(char *dst, size_t len, const char *posix_path)
{
    if (dst == nullptr || len == 0) {
        return false;
    }
    if (posix_path == nullptr || posix_path[0] == '\0') {
        dst[0] = '\0';
        return false;
    }
    const int n = snprintf(dst, len, "%c:%s", kFsLetter, posix_path);
    if (n < 0 || (size_t)n >= len) {
        dst[0] = '\0';
        return false;
    }
    return true;
}

}  // namespace ui
