#include "storage_service.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sd_card.h"
#include "str_util.h"

#include "esp_log.h"

static const char *TAG = "storage";

namespace services {

namespace {

bool s_ready = false;

/* qsort comparator: directories first, then case-insensitive name. */
int entry_cmp(const void *a, const void *b)
{
    const DirEntry *x = static_cast<const DirEntry *>(a);
    const DirEntry *y = static_cast<const DirEntry *>(b);
    if (x->is_dir != y->is_dir) {
        return x->is_dir ? -1 : 1;
    }
    return strcasecmp(x->name, y->name);
}

}  // namespace

esp_err_t storage_init()
{
    if (s_ready) {
        return ESP_OK;
    }
    esp_err_t err = sd_card_init();
    s_ready = (err == ESP_OK);
    return err;
}

bool storage_ready()
{
    /* Re-checked against the platform layer rather than cached forever: a card
     * can be pulled at any time and every page must cope. */
    s_ready = sd_card_mounted();
    return s_ready;
}

esp_err_t storage_capacity(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (!storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    return sd_card_capacity(total_bytes, free_bytes);
}

esp_err_t storage_list(const char *path, DirEntry *out, size_t max, size_t *total)
{
    if (total != nullptr) {
        *total = 0;
    }
    if (path == nullptr || out == nullptr || max == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }

    DIR *dir = opendir(path);
    if (dir == nullptr) {
        ESP_LOGW(TAG, "opendir(%s) failed", path);
        return ESP_ERR_NOT_FOUND;
    }

    size_t n = 0;        /* entries stored in out[]                       */
    size_t seen = 0;     /* entries that exist, including ones we skipped */
    struct dirent *de = nullptr;
    while ((de = readdir(dir)) != nullptr) {
        if (de->d_name[0] == '.') {
            continue;   /* skip "." / ".." / hidden */
        }
        ++seen;
        if (n >= max) {
            /* Keep counting so the caller can honestly say "40 of 210"
             * instead of presenting a truncated folder as a short one. */
            continue;
        }
        DirEntry &e = out[n];
        /* FATFS filenames run to 255 bytes; DirEntry::name is 128.  The copy
         * truncates on purpose, and str_copy() is what tells the compiler so. */
        platform::str_copy(e.name, sizeof(e.name), de->d_name);

        char full[512];
        storage_join(full, sizeof(full), path, e.name);
        struct stat st;
        const bool have_stat = (stat(full, &st) == 0);

        /* FATFS's readdir does fill d_type, but the SPI-SD path has surprised
         * us before and DT_UNKNOWN is legal, so the attribute is cross-checked
         * against the filesystem rather than trusted. */
        if (de->d_type == DT_DIR) {
            e.is_dir = true;
        } else if (de->d_type == DT_REG) {
            e.is_dir = false;
        } else {
            e.is_dir = have_stat && S_ISDIR(st.st_mode);
        }
        e.size = (!e.is_dir && have_stat) ? (uint32_t)st.st_size : 0;

        ++n;
    }
    closedir(dir);

    if (n > 1) {
        qsort(out, n, sizeof(DirEntry), entry_cmp);
    }
    if (total != nullptr) {
        *total = seen;
    }
    return ESP_OK;
}

uint32_t storage_file_size(const char *path)
{
    if (path == nullptr || !storage_ready()) {
        return 0;
    }
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return (uint32_t)st.st_size;
}

bool storage_exists(const char *path)
{
    if (path == nullptr || !storage_ready()) {
        return false;
    }
    struct stat st;
    return stat(path, &st) == 0;
}

bool storage_is_dir(const char *path)
{
    if (path == nullptr || !storage_ready()) {
        return false;
    }
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

esp_err_t storage_write(const char *path, const void *data, size_t len)
{
    if (path == nullptr || data == nullptr || !storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    FILE *f = fopen(path, "wb");
    if (f == nullptr) {
        ESP_LOGE(TAG, "cannot open %s for writing", path);
        return ESP_FAIL;
    }
    const size_t written = fwrite(data, 1, len, f);
    fclose(f);
    if (written != len) {
        ESP_LOGE(TAG, "short write on %s (%u of %u)", path, (unsigned)written, (unsigned)len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t storage_read(const char *path, void *buf, size_t max, size_t *got)
{
    if (got != nullptr) {
        *got = 0;
    }
    if (path == nullptr || buf == nullptr || max == 0 || !storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    FILE *f = fopen(path, "rb");
    if (f == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }
    const size_t n = fread(buf, 1, max, f);
    fclose(f);
    if (got != nullptr) {
        *got = n;
    }
    return ESP_OK;
}

void *storage_open(const char *path)
{
    if (path == nullptr || !storage_ready()) {
        return nullptr;
    }
    return fopen(path, "rb");
}

esp_err_t storage_read_at(void *handle, size_t offset, void *buf, size_t len, size_t *got)
{
    if (got != nullptr) {
        *got = 0;
    }
    if (handle == nullptr || buf == nullptr || !storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    FILE *f = static_cast<FILE *>(handle);
    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        return ESP_FAIL;
    }
    const size_t n = fread(buf, 1, len, f);
    if (got != nullptr) {
        *got = n;
    }
    return (n == len) ? ESP_OK : ESP_FAIL;
}

void storage_close(void *handle)
{
    if (handle != nullptr) {
        fclose(static_cast<FILE *>(handle));
    }
}

esp_err_t storage_remove(const char *path)
{
    if (path == nullptr || !storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (storage_is_dir(path)) {
        return ESP_ERR_INVALID_ARG;   /* files only, by design */
    }
    return (remove(path) == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t storage_mkdir(const char *path)
{
    if (path == nullptr || !storage_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mkdir(path, 0777) == 0 || storage_is_dir(path)) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

void storage_join(char *dst, size_t len, const char *dir, const char *name)
{
    if (dst == nullptr || len == 0) {
        return;
    }
    if (dir == nullptr) {
        dir = "";
    }
    const size_t dlen = strlen(dir);
    const bool need_slash = (dlen > 0 && dir[dlen - 1] != '/');
    snprintf(dst, len, "%s%s%s", dir, need_slash ? "/" : "", name ? name : "");
}

void storage_extension(char *dst, size_t len, const char *name)
{
    if (dst == nullptr || len == 0) {
        return;
    }
    dst[0] = '\0';
    if (name == nullptr) {
        return;
    }
    const char *dot = strrchr(name, '.');
    if (dot == nullptr || dot[1] == '\0') {
        return;
    }
    size_t i = 0;
    for (++dot; *dot != '\0' && i + 1 < len; ++dot, ++i) {
        dst[i] = (char)tolower((unsigned char)*dot);
    }
    dst[i] = '\0';
}

bool storage_is_image(const char *name)
{
    char ext[8];
    storage_extension(ext, sizeof(ext), name);
    /* bmp is included because the LVGL build enables the BMP decoder, so a
     * .bmp really can be shown - listing only jpg/jpeg/png would hide files
     * the device is able to open. */
    return strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0 ||
           strcmp(ext, "png") == 0 || strcmp(ext, "bmp") == 0;
}

bool storage_is_text(const char *name)
{
    char ext[8];
    storage_extension(ext, sizeof(ext), name);
    return strcmp(ext, "txt") == 0 || strcmp(ext, "md") == 0 || strcmp(ext, "log") == 0;
}

bool storage_is_document(const char *name)
{
    char ext[8];
    storage_extension(ext, sizeof(ext), name);
    /* Formats the device can list, copy and delete but not open - which is
     * exactly why they are worth telling apart from a text file: one of the two
     * kinds can be read on this screen and the other cannot.  Kept as a list
     * rather than "anything not an image and not text" so that an unknown
     * extension stays a plain file instead of being promoted to a document. */
    static const char *const kDocs[] = {
        "pdf", "doc", "docx", "odt", "rtf", "epub",
        "xls", "xlsx", "ppt", "pptx", "html", "htm",
    };
    for (const char *d : kDocs) {
        if (strcmp(ext, d) == 0) {
            return true;
        }
    }
    return false;
}

const char *storage_basename(const char *path)
{
    if (path == nullptr) {
        return "";
    }
    const char *slash = strrchr(path, '/');
    return (slash != nullptr) ? slash + 1 : path;
}

void storage_human_size(char *dst, size_t len, uint64_t bytes)
{
    if (dst == nullptr || len == 0) {
        return;
    }
    if (bytes >= (1024ull * 1024ull)) {
        snprintf(dst, len, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024ull) {
        snprintf(dst, len, "%.0f KB", (double)bytes / 1024.0);
    } else {
        snprintf(dst, len, "%u B", (unsigned)bytes);
    }
}

}  // namespace services
