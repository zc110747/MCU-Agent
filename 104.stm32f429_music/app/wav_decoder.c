/**
  ******************************************************************************
  * @file    wav_decoder.c
  * @brief   RIFF/WAVE header walk + PCM streaming.
  *
  *  All multi-byte fields are read byte by byte: the header buffer is not
  *  guaranteed to be aligned at every chunk boundary, and unaligned 32-bit
  *  loads are undefined behaviour even though the Cortex-M4 tolerates most of
  *  them.
  ******************************************************************************
  */
#include <string.h>

#include "wav_decoder.h"
#include "log.h"

/* Enough for RIFF + fmt + any FACT/LIST/ID3 preamble a normal file carries. */
#define WAV_SCAN_BYTES      1024U
/* 24-bit input is read 3 bytes per sample and widened to 4. */
#define WAV_24BIT_CHUNK     3072U

#define WAV_ID_RIFF         0x46464952U   /* "RIFF" */
#define WAV_ID_WAVE         0x45564157U   /* "WAVE" */
#define WAV_ID_FMT          0x20746D66U   /* "fmt " */
#define WAV_ID_DATA         0x61746164U   /* "data" */

static uint8_t s_scan[WAV_SCAN_BYTES] __attribute__((aligned(4)));
static uint8_t s_pack24[WAV_24BIT_CHUNK] __attribute__((aligned(4)));

static uint16_t rd_u16(const uint8_t *p)
{
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p)
{
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool wav_parse_header(FIL *fp, WAV_INFO_T *info)
{
  UINT br = 0U;
  uint32_t offset;
  bool have_fmt = false;
  bool have_data = false;

  if ((fp == NULL) || (info == NULL))
  {
    return false;
  }

  memset(info, 0, sizeof(WAV_INFO_T));

  if (f_lseek(fp, 0U) != FR_OK)
  {
    return false;
  }
  if (f_read(fp, s_scan, WAV_SCAN_BYTES, &br) != FR_OK)
  {
    return false;
  }
  if (br < 12U)
  {
    return false;
  }
  if ((rd_u32(&s_scan[0]) != WAV_ID_RIFF) || (rd_u32(&s_scan[8]) != WAV_ID_WAVE))
  {
    return false;
  }

  offset = 12U;
  while ((offset + 8U) <= br)
  {
    uint32_t chunk_id = rd_u32(&s_scan[offset]);
    uint32_t chunk_size = rd_u32(&s_scan[offset + 4U]);
    uint32_t body = offset + 8U;

    if (chunk_id == WAV_ID_FMT)
    {
      if ((body + 16U) > br)
      {
        return false;
      }
      info->audio_format    = rd_u16(&s_scan[body + 0U]);
      info->channels        = rd_u16(&s_scan[body + 2U]);
      info->sample_rate     = rd_u32(&s_scan[body + 4U]);
      info->byte_rate       = rd_u32(&s_scan[body + 8U]);
      info->block_align     = rd_u16(&s_scan[body + 12U]);
      info->bits_per_sample = rd_u16(&s_scan[body + 14U]);
      have_fmt = true;
    }
    else if (chunk_id == WAV_ID_DATA)
    {
      info->data_offset = body;
      info->data_size   = chunk_size;
      have_data = true;
      break;
    }

    /* Chunks are word aligned: an odd size is followed by one pad byte. */
    offset = body + chunk_size + (chunk_size & 1U);
  }

  if (!have_fmt || !have_data)
  {
    PRINT_LOG("[WAV ] header incomplete (fmt=%u data=%u)\r\n",
              (unsigned)have_fmt, (unsigned)have_data);
    return false;
  }
  if (info->audio_format != 1U)
  {
    PRINT_LOG("[WAV ] only linear PCM is supported (format=%u)\r\n",
              (unsigned)info->audio_format);
    return false;
  }
  if ((info->bits_per_sample != 16U) && (info->bits_per_sample != 24U))
  {
    PRINT_LOG("[WAV ] unsupported bit depth %u\r\n", (unsigned)info->bits_per_sample);
    return false;
  }
  if ((info->byte_rate == 0U) || (info->data_size == 0U))
  {
    return false;
  }

  info->total_seconds = info->data_size / info->byte_rate;

  PRINT_LOG("[WAV ] %lu Hz %u ch %u bit, %lu s\r\n",
            (unsigned long)info->sample_rate,
            (unsigned)info->channels,
            (unsigned)info->bits_per_sample,
            (unsigned long)info->total_seconds);
  return true;
}

uint32_t wav_read_pcm(FIL *fp, uint8_t *dst, uint32_t bytes, uint16_t bps)
{
  UINT br = 0U;
  uint32_t produced;

  if ((fp == NULL) || (dst == NULL) || (bytes == 0U))
  {
    return 0U;
  }

  if (bps == 24U)
  {
    uint32_t samples = bytes / 4U;
    uint32_t raw = samples * 3U;
    uint32_t i;

    if (raw > WAV_24BIT_CHUNK)
    {
      raw = WAV_24BIT_CHUNK;
      samples = raw / 3U;
    }

    if (f_read(fp, s_pack24, raw, &br) != FR_OK)
    {
      br = 0U;
    }

    for (i = 0U; i < (br / 3U); i++)
    {
      /* 24-bit payload, MSB first, left-aligned in the 32-bit slot. */
      dst[i * 4U + 0U] = 0U;
      dst[i * 4U + 1U] = s_pack24[i * 3U + 0U];
      dst[i * 4U + 2U] = s_pack24[i * 3U + 1U];
      dst[i * 4U + 3U] = s_pack24[i * 3U + 2U];
    }
    produced = (br / 3U) * 4U;

    /* Zero-pad the tail so the DMA never replays stale audio. */
    if (produced < bytes)
    {
      memset(dst + produced, 0, bytes - produced);
    }
    return produced;
  }

  if (f_read(fp, dst, (UINT)bytes, &br) != FR_OK)
  {
    br = 0U;
  }
  produced = br;
  if (produced < bytes)
  {
    memset(dst + produced, 0, bytes - produced);
  }
  return produced;
}
