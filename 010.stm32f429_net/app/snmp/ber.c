/**
  ******************************************************************************
  * @file    ber.c
  * @brief   Minimal BER/ASN.1 (DER) codec implementation (see ber.h).
  ******************************************************************************
  */
#include "ber.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Decoder                                                             */
/* ------------------------------------------------------------------ */
void ber_dec_init(ber_dec_t *d, const uint8_t *buf, uint32_t len)
{
  d->p = buf;
  d->end = buf + len;
  d->err = 0;
}

/* Decode a definite length (short or long form). Returns 0 ok. */
static int dec_len(ber_dec_t *d, uint32_t *out)
{
  if (d->p >= d->end) { d->err = -1; return -1; }
  uint8_t b = *d->p++;
  if (b < 0x80)
  {
    *out = b;
    return 0;
  }
  uint8_t nbytes = b & 0x7F;
  if (nbytes == 0 || nbytes > 4) { d->err = -1; return -1; }  /* indefinite/invalid */
  uint32_t v = 0;
  for (uint8_t i = 0; i < nbytes; i++)
  {
    if (d->p >= d->end) { d->err = -1; return -1; }
    v = (v << 8) | *d->p++;
  }
  *out = v;
  return 0;
}

int ber_dec_tlv(ber_dec_t *d, ber_tlv_t *t)
{
  if (d->p >= d->end) { d->err = -1; return -1; }
  t->tag = *d->p++;
  if (dec_len(d, &t->len) != 0) return -1;
  if ((uint32_t)(d->end - d->p) < t->len) { d->err = -1; return -1; }
  t->val = d->p;
  t->next = d->p + t->len;
  d->p = t->next;
  return 0;
}

static int decode_int_bytes(const uint8_t *v, uint32_t n, int32_t *out)
{
  if (n == 0 || n > 5) return -1;            /* support up to 32-bit + sign */
  int32_t acc = (int32_t)(int8_t)v[0];       /* sign-extend first byte */
  for (uint32_t i = 1; i < n; i++)
  {
    if (acc > 0x7FFFFF || acc < (int32_t)0xFFFFFFF8L) { /* would overflow 32-bit range */
      /* allow one extra guard byte only when top bits are all sign */
    }
    acc = (acc << 8) | v[i];
  }
  *out = acc;
  return 0;
}

int ber_decode_integer(const ber_tlv_t *t, int32_t *out)
{
  if (t->len == 0 || t->len > 5) return -1;
  return decode_int_bytes(t->val, t->len, out);
}

int ber_decode_u32(const ber_tlv_t *t, uint32_t *out)
{
  if (t->len == 0 || t->len > 4) return -1;
  uint32_t acc = 0;
  for (uint32_t i = 0; i < t->len; i++)
    acc = (acc << 8) | t->val[i];
  *out = acc;
  return 0;
}

int ber_decode_octet(const ber_tlv_t *t, const uint8_t **out, uint32_t *outlen)
{
  *out = t->val;
  *outlen = t->len;
  return 0;
}

int ber_decode_oid(const ber_tlv_t *t, uint32_t *arcs, uint32_t *narcs)
{
  uint32_t n = 0;
  const uint8_t *p = t->val;
  const uint8_t *end = t->val + t->len;
  if (p >= end) return -1;

  /* first byte encodes 40*arc0 + arc1 */
  uint32_t first = *p++;
  uint32_t arc0 = first / 40;
  if (arc0 > 2) arc0 = 2;                      /* X.660 clamp */
  uint32_t arc1 = first - arc0 * 40;
  if (n < BER_OID_MAX_ARCS) arcs[n++] = arc0;
  if (n < BER_OID_MAX_ARCS) arcs[n++] = arc1;

  while (p < end)
  {
    uint32_t v = 0;
    uint8_t b;
    do {
      if (p >= end) return -1;
      b = *p++;
      v = (v << 7) | (b & 0x7F);
    } while (b & 0x80);
    if (n < BER_OID_MAX_ARCS) arcs[n++] = v;
    else { /* skip excess arcs but keep parsing to consume the value */ }
  }
  *narcs = n;
  return 0;
}

int ber_decode_ip(const ber_tlv_t *t, uint8_t out[4])
{
  if (t->len != 4) return -1;
  memcpy(out, t->val, 4);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Encoder                                                             */
/* ------------------------------------------------------------------ */
void ber_enc_init(ber_enc_t *e, uint8_t *buf, uint32_t len)
{
  e->start = buf;
  e->p = buf;
  e->end = buf + len;
  e->err = 0;
}

int ber_enc_len(ber_enc_t *e, uint32_t len)
{
  if (len < 0x80)
  {
    if (e->p + 1 > e->end) { e->err = -1; return -1; }
    *e->p++ = (uint8_t)len;
    return 0;
  }
  /* long form: count bytes needed */
  uint8_t tmp[4];
  int n = 0;
  uint32_t v = len;
  while (v > 0) { tmp[n++] = (uint8_t)(v & 0xFF); v >>= 8; }
  if (e->p + 1 + n > e->end) { e->err = -1; return -1; }
  *e->p++ = (uint8_t)(0x80 | n);
  while (n > 0) *e->p++ = tmp[--n];
  return 0;
}

int ber_enc_begin(ber_enc_t *e, uint8_t tag, uint32_t *marker)
{
  if (e->p + 1 > e->end) { e->err = -1; return -1; }
  *e->p++ = tag;
  *marker = (uint32_t)(e->p - e->start);   /* length field position */
  /* Reserve a 5-byte long-form length slot (0x84 + 4 length bytes). It is
   * back-patched by ber_enc_end() (long form) or shrunk to 1 byte (short
   * form) via memmove. 5 bytes are reserved so the long form never writes
   * past the slot. */
  if (e->p + 5 > e->end) { e->err = -1; return -1; }
  e->p += 5;
  return 0;
}

int ber_enc_end(ber_enc_t *e, uint32_t marker)
{
  uint32_t content_len = (uint32_t)(e->p - e->start) - marker - 5;
  /* write length in the reserved 5-byte slot (long form: 0x84 + 4 bytes) */
  uint8_t *lp = e->start + marker;
  lp[0] = (uint8_t)(0x80 | 4);
  lp[1] = (uint8_t)((content_len >> 24) & 0xFF);
  lp[2] = (uint8_t)((content_len >> 16) & 0xFF);
  lp[3] = (uint8_t)((content_len >> 8) & 0xFF);
  lp[4] = (uint8_t)(content_len & 0xFF);
  /* compact: if content_len < 0x80, switch to short form. The length field
   * occupies 5 bytes (marker..marker+4); content starts at marker+5. We drop
   * the 4 extra length bytes by shifting content back to marker+1. */
  if (content_len < 0x80)
  {
    uint8_t *src = e->start + marker + 5;          /* start of content */
    uint32_t clen = (uint32_t)(e->p - src);
    memmove(e->start + marker + 1, src, clen);
    e->p = e->start + marker + 1 + clen;
    e->start[marker] = (uint8_t)content_len;
  }
  return 0;
}

int ber_enc_tlv_byte(ber_enc_t *e, uint8_t tag, const uint8_t *val, uint32_t vlen)
{
  if (e->p + 1 > e->end) { e->err = -1; return -1; }
  *e->p++ = tag;
  if (ber_enc_len(e, vlen) != 0) return -1;
  if (e->p + vlen > e->end) { e->err = -1; return -1; }
  if (vlen) memcpy(e->p, val, vlen);
  e->p += vlen;
  return 0;
}

int ber_enc_integer(ber_enc_t *e, int32_t v)
{
  /* minimal 2's complement encoding (strip redundant leading bytes). */
  uint8_t buf[5];
  uint32_t uv = (uint32_t)v;
  buf[4] = (uint8_t)(uv & 0xFF);
  buf[3] = (uint8_t)((uv >> 8) & 0xFF);
  buf[2] = (uint8_t)((uv >> 16) & 0xFF);
  buf[1] = (uint8_t)((uv >> 24) & 0xFF);
  buf[0] = (uint8_t)((uv >> 24) & 0x80) ? 0xFF : 0x00; /* sign byte */
  int start = 0;
  /* keep at least one byte; trim redundant sign-extension bytes */
  while (start < 3 &&
         ((buf[start] == 0x00 && (buf[start+1] & 0x80) == 0) ||
          (buf[start] == 0xFF && (buf[start+1] & 0x80) != 0)))
    start++;
  return ber_enc_tlv_byte(e, BER_TAG_INTEGER, buf + start, (uint32_t)(5 - start));
}

int ber_enc_u32_app(ber_enc_t *e, uint8_t apptag, uint32_t v)
{
  uint8_t tag = (uint8_t)(BER_CLASS_APPLICATION | apptag);
  uint8_t buf[4];
  int start = 0;
  buf[3] = (uint8_t)(v & 0xFF);
  buf[2] = (uint8_t)((v >> 8) & 0xFF);
  buf[1] = (uint8_t)((v >> 16) & 0xFF);
  buf[0] = (uint8_t)((v >> 24) & 0xFF);
  while (start < 3 && buf[start] == 0x00) start++;
  return ber_enc_tlv_byte(e, tag, buf + start, (uint32_t)(4 - start));
}

int ber_enc_octet(ber_enc_t *e, const uint8_t *s, uint32_t slen)
{
  return ber_enc_tlv_byte(e, BER_TAG_OCTET_STRING, s, slen);
}

int ber_enc_null(ber_enc_t *e)
{
  return ber_enc_tlv_byte(e, BER_TAG_NULL, NULL, 0);
}

int ber_enc_ip(ber_enc_t *e, const uint8_t ip[4])
{
  uint8_t tag = (uint8_t)(BER_CLASS_APPLICATION | BER_TAG_IPADDRESS);
  return ber_enc_tlv_byte(e, tag, ip, 4);
}

int ber_enc_oid(ber_enc_t *e, const uint32_t *arcs, uint32_t narcs)
{
  if (narcs < 2) return -1;
  uint8_t buf[BER_OID_MAX_ARCS * 5];
  int n = 0;
  /* first byte: 40*arc0 + arc1 */
  uint32_t first = 40 * arcs[0] + arcs[1];
  if (first > 255) first = 255;
  buf[n++] = (uint8_t)first;
  for (uint32_t i = 2; i < narcs; i++)
  {
    uint32_t v = arcs[i];
    uint8_t tmp[5];
    int k = 0;
    if (v == 0) { tmp[k++] = 0; }
    else {
      while (v > 0) { tmp[k++] = (uint8_t)(v & 0x7F); v >>= 7; }
    }
    /* emit big-endian with continuation bits */
    for (int j = k - 1; j >= 0; j--)
    {
      uint8_t b = tmp[j];
      if (j > 0) b |= 0x80;
      buf[n++] = b;
    }
  }
  return ber_enc_tlv_byte(e, BER_TAG_OID, buf, (uint32_t)n);
}

uint32_t ber_enc_len_written(const ber_enc_t *e)
{
  return (uint32_t)(e->p - e->start);
}
