/**
  ******************************************************************************
  * @file    ber.h
  * @brief   Minimal BER/ASN.1 (DER) codec for SNMP v1/v2c.
  *
  *          Only the subset used by SNMP is implemented:
  *            INTEGER, OCTET STRING, OBJECT IDENTIFIER, NULL,
  *            SEQUENCE, SEQUENCE OF, IpAddress (app 0x40),
  *            Counter32 / Gauge32 / TimeTicks (app 0x41/0x42/0x43),
  *            opaque integer helpers.
  *
  *          The encoder writes into a caller-supplied buffer (no heap).
  *          The decoder is a single-pass cursor over a const buffer.
  *
  *          OID encoding note: the first two arcs are packed as
  *          40*arc0 + arc1 into one byte; subsequent arcs use the
  *          base-128 (big-endian, continuation bit) scheme.
  ******************************************************************************
  */
#ifndef __BER_H__
#define __BER_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Tag classes / PC bit (only what SNMP needs) ---- */
#define BER_CLASS_UNIVERSAL   0x00
#define BER_CLASS_APPLICATION 0x40
#define BER_CONSTRUCTED       0x20

/* ---- Universal tags ---- */
#define BER_TAG_INTEGER        0x02
#define BER_TAG_OCTET_STRING   0x04
#define BER_TAG_NULL           0x05
#define BER_TAG_OID            0x06
#define BER_TAG_SEQUENCE       0x30   /* UNIVERSAL SEQUENCE (constructed) */

/* ---- Application tags (SNMP) ---- */
#define BER_TAG_IPADDRESS      0x00   /* 0x40 */
#define BER_TAG_COUNTER32      0x01   /* 0x41 */
#define BER_TAG_GAUGE32        0x02   /* 0x42 */
#define BER_TAG_TIMETICKS      0x03   /* 0x43 */
#define BER_TAG_OPAQUE         0x04
#define BER_TAG_COUNTER64      0x06   /* 0x46 */

/* ---- SNMP PDU tags (context-specific, constructed) ---- */
#define BER_TAG_GetRequest     0xA0
#define BER_TAG_GetNextRequest 0xA1
#define BER_TAG_GetResponse    0xA2
#define BER_TAG_SetRequest     0xA3
#define BER_TAG_Trap           0xA4   /* v1 trap */
#define BER_TAG_GetBulkRequest 0xA5   /* v2c */
#define BER_TAG_Report         0xA8   /* v2c (inform/agentx internal) */

/* Max OID length in arcs (root 1.3.6.1.4.1.32.x.y...). */
#define BER_OID_MAX_ARCS  32

/* ------------------------------------------------------------------ */
/* Decoder cursor                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
  const uint8_t *p;     /* current read pointer */
  const uint8_t *end;   /* one past last valid byte */
  int            err;   /* 0 = ok, <0 = error code */
} ber_dec_t;

/* A decoded TLV header. */
typedef struct {
  uint8_t  tag;
  uint32_t len;
  const uint8_t *val;   /* points at the value bytes */
  const uint8_t *next;  /* points just after the value */
} ber_tlv_t;

void     ber_dec_init(ber_dec_t *d, const uint8_t *buf, uint32_t len);
/* Peek/decode the next TLV at the cursor, advancing it. Returns 0 ok, -1 eof/err. */
int      ber_dec_tlv(ber_dec_t *d, ber_tlv_t *t);

/* Decode primitive helpers (call after ber_dec_tlv with the right tag). */
int      ber_decode_integer(const ber_tlv_t *t, int32_t *out);   /* signed */
int      ber_decode_u32(const ber_tlv_t *t, uint32_t *out);      /* unsigned, len<=4 */
int      ber_decode_octet(const ber_tlv_t *t, const uint8_t **out, uint32_t *outlen);
int      ber_decode_oid(const ber_tlv_t *t, uint32_t *arcs, uint32_t *narcs);
int      ber_decode_ip(const ber_tlv_t *t, uint8_t out[4]);

/* ------------------------------------------------------------------ */
/* Encoder                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
  uint8_t *p;          /* current write pointer */
  uint8_t *start;
  uint8_t *end;
  int      err;
} ber_enc_t;

void     ber_enc_init(ber_enc_t *e, uint8_t *buf, uint32_t len);
/* Write a length (definite, short or long form). */
int      ber_enc_len(ber_enc_t *e, uint32_t len);
/* Begin a constructed TLV: writes tag + placeholder length, returns a marker
 * to be fixed up by ber_enc_end(). */
int      ber_enc_begin(ber_enc_t *e, uint8_t tag, uint32_t *marker);
int      ber_enc_end(ber_enc_t *e, uint32_t marker);   /* back-patch length */
/* Primitive writers. */
int      ber_enc_tlv_byte(ber_enc_t *e, uint8_t tag, const uint8_t *val, uint32_t vlen);
int      ber_enc_integer(ber_enc_t *e, int32_t v);
int      ber_enc_u32_app(ber_enc_t *e, uint8_t apptag, uint32_t v);
int      ber_enc_octet(ber_enc_t *e, const uint8_t *s, uint32_t slen);
int      ber_enc_oid(ber_enc_t *e, const uint32_t *arcs, uint32_t narcs);
int      ber_enc_null(ber_enc_t *e);
int      ber_enc_ip(ber_enc_t *e, const uint8_t ip[4]);
/* Total bytes written so far. */
uint32_t ber_enc_len_written(const ber_enc_t *e);

#ifdef __cplusplus
}
#endif

#endif /* __BER_H__ */
