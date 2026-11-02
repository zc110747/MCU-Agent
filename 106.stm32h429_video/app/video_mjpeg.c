/**
 ******************************************************************************
 * @file    video_mjpeg.c
 * @brief   MJPEG frame decoder.
 *
 *          Ported from the ALIENTEK 实验47 example.  The reference decoder
 *          pushed every scanline straight into the LCD (DMA2D on RGB panels,
 *          GRAM writes on MCU panels); here the row converter in
 *          third_party/jpeg/jdmerge.c calls mjpeg_row_emit() instead, and this
 *          file copies the scanline into the RGB565 surface.
 *
 *          libjpeg asks jmemnobs.c for memory, which forwards to
 *          mjpeg_pool_alloc() here: a bump allocator over one 38 KB block that
 *          is reset at the start of every frame.  libjpeg's free calls are
 *          no-ops by design (jmemnobs.c keeps the same contract).
 ******************************************************************************
 */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "jpeglib.h"

#include "mjpeg_row.h"
#include "video_mjpeg.h"
#include "log.h"

#include "FreeRTOS.h"

/* libjpeg's working set for one 800x480 MJPEG frame; the reference example
 * proved 38 KB is enough for the AVI sample clips. */
#define MJPEG_POOL_BYTES (38U * 1024U)

struct my_error_mgr
{
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

typedef struct my_error_mgr *my_error_ptr;

typedef struct
{
    struct jpeg_decompress_struct *cinfo;
    struct my_error_mgr *jerr;
    uint8_t *pool;
    uint32_t pool_pos;
    uint16_t *row;
    uint8_t *scanline;
    const uint8_t *jpeg_data;
    uint32_t jpeg_size;
    uint16_t *surface;
    uint16_t surface_w;
    uint16_t surface_h;
    uint16_t off_x;
    uint16_t off_y;
    uint16_t row_index;
    uint8_t ready;
} mjpeg_state_t;

static mjpeg_state_t s_mjpeg = {0};

/* ------------------------------------------------------------------ */
/* Row sink used by third_party/jpeg/jdmerge.c                        */
/* ------------------------------------------------------------------ */

uint16_t *mjpeg_row_buffer(void)
{
    return s_mjpeg.row;
}

void mjpeg_row_emit(const uint16_t *row, uint32_t width)
{
    uint32_t y;
    uint32_t copy_w;

    if (s_mjpeg.ready == 0U)
    {
        return;
    }

    y = (uint32_t)s_mjpeg.off_y + s_mjpeg.row_index;
    if (y >= s_mjpeg.surface_h)
    {
        return;
    }

    copy_w = width;
    if ((uint32_t)s_mjpeg.off_x + copy_w > s_mjpeg.surface_w)
    {
        copy_w = (uint32_t)s_mjpeg.surface_w - s_mjpeg.off_x;
    }

    memcpy(&s_mjpeg.surface[(y * s_mjpeg.surface_w) + s_mjpeg.off_x],
           row, copy_w * sizeof(uint16_t));
    s_mjpeg.row_index++;
}

void *mjpeg_pool_alloc(size_t size)
{
    uint32_t start = s_mjpeg.pool_pos;

    if ((s_mjpeg.pool == NULL) || ((start + (uint32_t)size) > MJPEG_POOL_BYTES))
    {
        PRINT_LOG("[MJPG] pool exhausted (pos=%lu need=%lu)\r\n",
                  (unsigned long)start, (unsigned long)size);
        return NULL;
    }

    s_mjpeg.pool_pos = start + (uint32_t)size;
    return (void *)&s_mjpeg.pool[start];
}

/* ------------------------------------------------------------------ */
/* libjpeg error path                                                 */
/* ------------------------------------------------------------------ */

static void my_error_exit(j_common_ptr cinfo)
{
    my_error_ptr myerr = (my_error_ptr)cinfo->err;

    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

static void my_emit_message(j_common_ptr cinfo, int msg_level)
{
    my_error_ptr myerr = (my_error_ptr)cinfo->err;

    if (msg_level < 0)
    {
        PRINT_LOG("[MJPG] corrupt frame, code %d\r\n", msg_level);
        longjmp(myerr->setjmp_buffer, 1);
    }
}

/* ------------------------------------------------------------------ */
/* Data source: the whole JPEG payload is already in RAM              */
/* ------------------------------------------------------------------ */

static void init_source(j_decompress_ptr cinfo)
{
    (void)cinfo;
}

static boolean fill_input_buffer(j_decompress_ptr cinfo)
{
    if (s_mjpeg.jpeg_size == 0U)
    {
        /* Truncated frame: hand libjpeg an EOI so it stops cleanly. */
        static JOCTET eoi[2] = { 0xFF, JPEG_EOI };

        cinfo->src->next_input_byte = eoi;
        cinfo->src->bytes_in_buffer = 2;
    }
    else
    {
        cinfo->src->next_input_byte = s_mjpeg.jpeg_data;
        cinfo->src->bytes_in_buffer = s_mjpeg.jpeg_size;
        s_mjpeg.jpeg_size = 0U;
    }
    return TRUE;
}

static void skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
    if (num_bytes > 0)
    {
        while (num_bytes > (long)cinfo->src->bytes_in_buffer)
        {
            num_bytes -= (long)cinfo->src->bytes_in_buffer;
            (void)cinfo->src->fill_input_buffer(cinfo);
        }
        cinfo->src->next_input_byte += (size_t)num_bytes;
        cinfo->src->bytes_in_buffer -= (size_t)num_bytes;
    }
}

static void term_source(j_decompress_ptr cinfo)
{
    (void)cinfo;
}

static void jpeg_memory_src(j_decompress_ptr cinfo)
{
    if (cinfo->src == NULL)
    {
        cinfo->src = (struct jpeg_source_mgr *)(*cinfo->mem->alloc_small)(
            (j_common_ptr)cinfo, JPOOL_PERMANENT, sizeof(struct jpeg_source_mgr));
    }
    cinfo->src->init_source = init_source;
    cinfo->src->fill_input_buffer = fill_input_buffer;
    cinfo->src->skip_input_data = skip_input_data;
    cinfo->src->resync_to_restart = jpeg_resync_to_restart;
    cinfo->src->term_source = term_source;
    cinfo->src->bytes_in_buffer = 0;
    cinfo->src->next_input_byte = NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int8_t video_mjpeg_open(uint16_t *surface, uint16_t surface_w,
                        uint16_t surface_h, uint16_t off_x, uint16_t off_y)
{
    if ((surface == NULL) || (surface_w == 0U) || (surface_h == 0U))
    {
        return 1;
    }

    video_mjpeg_close();

    s_mjpeg.cinfo = (struct jpeg_decompress_struct *)pvPortMalloc(
        sizeof(struct jpeg_decompress_struct));
    s_mjpeg.jerr = (struct my_error_mgr *)pvPortMalloc(sizeof(struct my_error_mgr));
    s_mjpeg.pool = (uint8_t *)pvPortMalloc(MJPEG_POOL_BYTES);
    s_mjpeg.row = (uint16_t *)pvPortMalloc((size_t)surface_w * sizeof(uint16_t));
    /* Scanline target for libjpeg's non-merged path (4:4:4 chroma).  The
     * merged path ignores it, but a 4:4:4 frame would otherwise hand a NULL
     * buffer to jdcolor/jdsample and fault. */
    s_mjpeg.scanline = (uint8_t *)pvPortMalloc((size_t)surface_w * 3U);

    if ((s_mjpeg.cinfo == NULL) || (s_mjpeg.jerr == NULL) ||
        (s_mjpeg.pool == NULL) || (s_mjpeg.row == NULL) ||
        (s_mjpeg.scanline == NULL))
    {
        PRINT_LOG("[MJPG] out of memory (SDRAM heap)\r\n");
        video_mjpeg_close();
        return 1;
    }

    memset(s_mjpeg.cinfo, 0, sizeof(struct jpeg_decompress_struct));
    memset(s_mjpeg.jerr, 0, sizeof(struct my_error_mgr));

    s_mjpeg.surface = surface;
    s_mjpeg.surface_w = surface_w;
    s_mjpeg.surface_h = surface_h;
    s_mjpeg.off_x = off_x;
    s_mjpeg.off_y = off_y;
    s_mjpeg.pool_pos = 0U;
    s_mjpeg.row_index = 0U;
    s_mjpeg.ready = 1U;

    return 0;
}

void video_mjpeg_close(void)
{
    if (s_mjpeg.cinfo != NULL)
    {
        vPortFree(s_mjpeg.cinfo);
        s_mjpeg.cinfo = NULL;
    }
    if (s_mjpeg.jerr != NULL)
    {
        vPortFree(s_mjpeg.jerr);
        s_mjpeg.jerr = NULL;
    }
    if (s_mjpeg.pool != NULL)
    {
        vPortFree(s_mjpeg.pool);
        s_mjpeg.pool = NULL;
    }
    if (s_mjpeg.row != NULL)
    {
        vPortFree(s_mjpeg.row);
        s_mjpeg.row = NULL;
    }
    if (s_mjpeg.scanline != NULL)
    {
        vPortFree(s_mjpeg.scanline);
        s_mjpeg.scanline = NULL;
    }
    s_mjpeg.ready = 0U;
}

int8_t video_mjpeg_decode(const uint8_t *data, uint32_t size)
{
    if ((s_mjpeg.ready == 0U) || (size == 0U))
    {
        return 1;
    }

    s_mjpeg.jpeg_data = data;
    s_mjpeg.jpeg_size = size;
    s_mjpeg.pool_pos = 0U;
    s_mjpeg.row_index = 0U;

    s_mjpeg.cinfo->err = jpeg_std_error(&s_mjpeg.jerr->pub);
    s_mjpeg.jerr->pub.error_exit = my_error_exit;
    s_mjpeg.jerr->pub.emit_message = my_emit_message;

    if (setjmp(s_mjpeg.jerr->setjmp_buffer) != 0)
    {
        jpeg_abort_decompress(s_mjpeg.cinfo);
        jpeg_destroy_decompress(s_mjpeg.cinfo);
        return 2;
    }

    jpeg_create_decompress(s_mjpeg.cinfo);
    jpeg_memory_src(s_mjpeg.cinfo);
    (void)jpeg_read_header(s_mjpeg.cinfo, TRUE);

    /* Speed over quality: this decodes video frames, not photographs. */
    s_mjpeg.cinfo->dct_method = JDCT_IFAST;
    s_mjpeg.cinfo->do_fancy_upsampling = FALSE;

    (void)jpeg_start_decompress(s_mjpeg.cinfo);

    /* Output rows are produced by jdmerge.c's merged upsampler, which calls
     * mjpeg_row_emit() once per scanline; the scanline loop below only drives
     * the pipeline. */
    while (s_mjpeg.cinfo->output_scanline < s_mjpeg.cinfo->output_height)
    {
        JSAMPROW rows[1];

        rows[0] = (JSAMPROW)s_mjpeg.scanline;
        (void)jpeg_read_scanlines(s_mjpeg.cinfo, rows, 1);
    }

    (void)jpeg_finish_decompress(s_mjpeg.cinfo);
    jpeg_destroy_decompress(s_mjpeg.cinfo);
    return 0;
}
