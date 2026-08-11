/**
  ******************************************************************************
  * @file    sd_browser.h
  * @brief   Shared "pick a file off the SD card" list, reused by the NES page
  *          and the image viewer.
  *
  *  Both features need the same thing: show the contents of a directory, let
  *  the user move a highlight with UP/DOWN, confirm with A/OK/START, and show
  *  a "cannot open" overlay on failure.  This keeps that logic in one place so
  *  the two pages only differ in what they do *after* a file is chosen.
  ******************************************************************************
  */
#ifndef SD_BROWSER_H
#define SD_BROWSER_H

#include "lvgl.h"
#include "bsp_key.h"
#include "app_page.h"
#include <stdint.h>

typedef struct sd_browser sd_browser_t;

/* Called when the user confirms a file.  name_gbk is the verbatim card name
 * (correct byte-for-byte for f_open); name_utf8 is the display form (GBK ->
 * UTF-8, which is what the GBK LVGL font driver wants). */
typedef void (*sd_select_cb)(int index,
                             const char *name_gbk,
                             const char *name_utf8,
                             void *ctx);

/* Scan `path` (e.g. "1:") for files and build an LVGL list into `root`.
 * Returns the browser handle, or NULL on out-of-memory. */
sd_browser_t *sd_browser_create(lv_obj_t *root, const char *path,
                                const char *title,
                                int rows_visible, int row_h,
                                sd_select_cb on_select, void *ctx);

/* Tear down (frees the handle; the page cleans `root` on exit). */
void sd_browser_destroy(sd_browser_t *b);

/* Re-scan the directory and rebuild the list in place. */
void sd_browser_refresh(sd_browser_t *b);

/* Handle a key.  Returns 1 if consumed (UP/DOWN navigate, A/OK/START select).
 * SELECT / BACK are intentionally NOT consumed here - the owning page decides
 * what "back" means. */
int  sd_browser_key(sd_browser_t *b, key_id_t id, key_edge_t edge);

int         sd_browser_count(sd_browser_t *b);
const char *sd_browser_name_gbk(sd_browser_t *b, int i);
const char *sd_browser_name_utf8(sd_browser_t *b, int i);

/* Current directory the browser is showing (e.g. "1:" or "1:/MUSIC").  Valid
 * only while the browser is alive; use it to build a full path when a file is
 * selected (the on_select callback only receives the bare file name). */
const char *sd_browser_path(sd_browser_t *b);

/* True if entry `i` is a directory (the on_select callback is only ever fired
 * for files, but a page may want to show this). */
int         sd_browser_is_dir(sd_browser_t *b, int i);

/* Error overlay (covers the list, opaque).  SELECT dismisses it - the page's
 * key handler checks sd_browser_is_error() and calls sd_browser_hide_error(). */
void sd_browser_show_error(sd_browser_t *b, const char *line1, const char *line2);
void sd_browser_hide_error(sd_browser_t *b);
int  sd_browser_is_error(sd_browser_t *b);

#endif /* SD_BROWSER_H */
