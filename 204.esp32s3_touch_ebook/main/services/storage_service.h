/**
 * @file storage_service.h
 * @brief Everything the UI needs from the SD card.
 *
 * Pages never call sd_card_* or fopen() directly: they ask for a listing, a
 * file's bytes or a path, and this layer owns the mount state, the path
 * joining and the "is the card even there" question.  That keeps the
 * error/empty states in the pages down to a single boolean.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

namespace services {

/** Longest filename we keep (enough for 8.3 and then some, incl. the NUL). */
constexpr size_t kNameMax = 128;

struct DirEntry {
    char     name[kNameMax];
    bool     is_dir;
    uint32_t size;
};

constexpr size_t kMaxEntriesPerPage = 64;

/** @brief Mount the card if it is not mounted yet. Never fatal. */
esp_err_t storage_init();

/** @brief True when a card is mounted and usable. */
bool storage_ready();

/** @brief Capacity of the mounted card. */
esp_err_t storage_capacity(uint64_t *total_bytes, uint64_t *free_bytes);

/**
 * @brief List a directory.
 *
 * Directories come first, then files, each group sorted by name, so a page can
 * render straight down without re-ordering.  At most @p max entries are *stored*
 * in @p out, but @p total receives the number that actually exist in the
 * directory - so a caller that fills its buffer can still tell the user
 * "40 of 210" rather than presenting a truncated folder as a short one.
 */
esp_err_t storage_list(const char *path, DirEntry *out, size_t max, size_t *total);

/** @brief A file's size, or 0 when it does not exist. */
uint32_t storage_file_size(const char *path);

bool storage_exists(const char *path);
bool storage_is_dir(const char *path);

/** @brief Write (creating or truncating) a whole file. */
esp_err_t storage_write(const char *path, const void *data, size_t len);

/** @brief Read a whole file, up to @p max bytes. Caller owns nothing. */
esp_err_t storage_read(const char *path, void *buf, size_t max, size_t *got);

/**
 * @brief Open a file for repeated random reads (e.g. a bitmap font read glyph
 *        by glyph). Returns an opaque handle, or nullptr on failure.
 *
 * The handle is NOT thread-exclusive; ESP-IDF's FatFs VFS serialises every
 * f_seek/f_read through one global lock, so the display task fetching a glyph
 * and the app task listing a directory cannot corrupt each other.  It is the
 * caller's job to storage_close() it when the font is torn down.
 */
void *storage_open(const char *path);

/** @brief Read @p len bytes at @p offset from a storage_open() handle. */
esp_err_t storage_read_at(void *handle, size_t offset, void *buf, size_t len, size_t *got);

/** @brief Close a handle returned by storage_open(). */
void storage_close(void *handle);

/** @brief Remove a file. Never removes directories. */
esp_err_t storage_remove(const char *path);

/** @brief Create a directory (one level). */
esp_err_t storage_mkdir(const char *path);

/* ---- path helpers ------------------------------------------------------- */

/** @brief Join with a single '/', writing into @p dst. Safe on overflow. */
void storage_join(char *dst, size_t len, const char *dir, const char *name);

/** @brief Lower-case extension without the dot, e.g. "jpg". Empty when none. */
void storage_extension(char *dst, size_t len, const char *name);

bool storage_is_image(const char *name);
bool storage_is_text(const char *name);

/**
 * @brief True for a document format the device can list but cannot open.
 *
 * Kept apart from storage_is_text() because that difference is what the file
 * list has to show: a .txt can be read on this screen, a .pdf cannot.  An
 * unknown extension is deliberately neither - inventing a document out of an
 * unrecognised suffix would make the icon mean nothing.
 */
bool storage_is_document(const char *name);

/** @brief Strip everything up to the last '/' (mutates a copy). */
const char *storage_basename(const char *path);

/** @brief "1.4 MB" / "812 KB" / "90 B". */
void storage_human_size(char *dst, size_t len, uint64_t bytes);

}  // namespace services
