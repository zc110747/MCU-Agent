/**
  ******************************************************************************
  * @file    snmp_msg.c
  * @brief   SNMP v2c message handler: decode request, dispatch MIB, encode
  *          response. See snmp_msg.h. (Extracted from snmp_agent.c so it can
  *          be unit-tested on the PC without lwIP.)
  ******************************************************************************
  */
#include "snmp_msg.h"
#include "mib.h"
#include "ber.h"
#include "log.h"

#include <string.h>
#include <stdio.h>

/* SNMP version enum: v1=0, v2c=1, v3=3 (RFC 1901/3411). */
#define SNMP_VERSION_v2c   1

#define SNMP_COMMUNITY     "public"
#define SNMP_COMMUNITY_LEN 6

#define SNMP_MAX_VARBINDS  16

/* ------------------------------------------------------------------ */
/* Message builder                                                     */
/* ------------------------------------------------------------------ */
static int build_response(uint8_t *out, uint32_t outcap,
                          int32_t req_id, uint8_t pdu_type,
                          uint8_t err_status, uint8_t err_index,
                          const uint32_t (*oids)[BER_OID_MAX_ARCS],
                          const uint32_t *narcs,
                          const mib_val_t *vals, uint32_t nvb)
{
  ber_enc_t e;
  ber_enc_init(&e, out, outcap);

  uint32_t m_msg;
  if (ber_enc_begin(&e, BER_TAG_SEQUENCE, &m_msg) != 0) return -1;

  if (ber_enc_integer(&e, SNMP_VERSION_v2c) != 0) return -1;
  if (ber_enc_octet(&e, (const uint8_t*)SNMP_COMMUNITY, SNMP_COMMUNITY_LEN) != 0) return -1;

  uint32_t m_pdu;
  if (ber_enc_begin(&e, BER_TAG_GetResponse, &m_pdu) != 0) return -1;
  if (ber_enc_integer(&e, req_id) != 0) return -1;
  if (ber_enc_integer(&e, (int32_t)err_status) != 0) return -1;
  if (ber_enc_integer(&e, (int32_t)err_index) != 0) return -1;

  uint32_t m_vbl;
  if (ber_enc_begin(&e, BER_TAG_SEQUENCE, &m_vbl) != 0) return -1;
  for (uint32_t i = 0; i < nvb; i++)
  {
    if (mib_encode_varbind(&e, oids[i], narcs[i], &vals[i]) != 0) return -1;
  }
  ber_enc_end(&e, m_vbl);
  ber_enc_end(&e, m_pdu);
  ber_enc_end(&e, m_msg);

  if (e.err != 0) return -1;
  return (int)ber_enc_len_written(&e);
}

/* ------------------------------------------------------------------ */
/* Request parser + dispatch                                           */
/* ------------------------------------------------------------------ */
int handle_message(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t outcap)
{
  ber_dec_t d;
  ber_dec_init(&d, in, inlen);
  ber_tlv_t t;

  if (ber_dec_tlv(&d, &t) != 0 || t.tag != BER_TAG_SEQUENCE) return -1;

  /* Re-base the decoder on the message's content so subsequent ber_dec_tlv
   * calls walk the SEQUENCE members (the top-level TLV leaves d->p at the end
   * of the message, past the first child). */
  ber_dec_init(&d, t.val, t.len);

  int32_t version;
  if (ber_dec_tlv(&d, &t) != 0 || t.tag != BER_TAG_INTEGER) return -1;
  ber_decode_integer(&t, &version);
  if (version != SNMP_VERSION_v2c) return -1;

  const uint8_t *comm;
  uint32_t commlen;
  if (ber_dec_tlv(&d, &t) != 0 || t.tag != BER_TAG_OCTET_STRING) return -1;
  ber_decode_octet(&t, &comm, &commlen);
  if (commlen != SNMP_COMMUNITY_LEN ||
      memcmp(comm, SNMP_COMMUNITY, SNMP_COMMUNITY_LEN) != 0)
    return -1;

  if (ber_dec_tlv(&d, &t) != 0) return -1;
  uint8_t pdu_type = t.tag;
  if (pdu_type != BER_TAG_GetRequest &&
      pdu_type != BER_TAG_GetNextRequest &&
      pdu_type != BER_TAG_SetRequest)
    return -1;

  ber_dec_t pd;
  ber_dec_init(&pd, t.val, t.len);

  int32_t req_id;
  if (ber_dec_tlv(&pd, &t) != 0 || t.tag != BER_TAG_INTEGER) return -1;
  ber_decode_integer(&t, &req_id);

  int32_t err_status, err_index;
  if (ber_dec_tlv(&pd, &t) != 0 || t.tag != BER_TAG_INTEGER) return -1;
  ber_decode_integer(&t, &err_status);
  if (ber_dec_tlv(&pd, &t) != 0 || t.tag != BER_TAG_INTEGER) return -1;
  ber_decode_integer(&t, &err_index);

  if (ber_dec_tlv(&pd, &t) != 0 || t.tag != BER_TAG_SEQUENCE) return -1;
  ber_dec_t vbl;
  ber_dec_init(&vbl, t.val, t.len);

  uint32_t oids[SNMP_MAX_VARBINDS][BER_OID_MAX_ARCS];
  uint32_t narcs[SNMP_MAX_VARBINDS];
  mib_val_t vals[SNMP_MAX_VARBINDS];
  uint32_t nvb = 0;

  uint8_t reply_err = SNMP_ERR_NOERROR;
  uint8_t reply_idx = 0;

  mib_stats_inc_req();

  while (nvb < SNMP_MAX_VARBINDS)
  {
    ber_tlv_t vb;
    if (ber_dec_tlv(&vbl, &vb) != 0) break;
    if (vb.tag != BER_TAG_SEQUENCE) break;

    ber_dec_t vbd;
    ber_dec_init(&vbd, vb.val, vb.len);

    ber_tlv_t ot;
    if (ber_dec_tlv(&vbd, &ot) != 0 || ot.tag != BER_TAG_OID) break;
    uint32_t arcs[BER_OID_MAX_ARCS];
    uint32_t n = 0;
    ber_decode_oid(&ot, arcs, &n);

    ber_tlv_t vt;
    if (ber_dec_tlv(&vbd, &vt) != 0) break;

    uint32_t idx = nvb;
    narcs[idx] = n;
    for (uint32_t k = 0; k < n && k < BER_OID_MAX_ARCS; k++) oids[idx][k] = arcs[k];

    if (pdu_type == BER_TAG_SetRequest)
    {
      mib_val_t sv;
      memset(&sv, 0, sizeof(sv));
      switch (vt.tag)
      {
        case BER_TAG_INTEGER:   ber_decode_integer(&vt, &sv.i); sv.type = MIB_T_INTEGER; break;
        case BER_TAG_OCTET_STRING: {
          const uint8_t *o; uint32_t ol; ber_decode_octet(&vt, &o, &ol);
          sv.type = MIB_T_OCTET; sv.oct = o; sv.octlen = ol; break;
        }
        default: sv.type = MIB_T_NULL; break;
      }
      int r = mib_set(arcs, n, &sv);
      if (r != SNMP_ERR_NOERROR && reply_err == SNMP_ERR_NOERROR)
      {
        reply_err = (uint8_t)r;
        reply_idx = (uint8_t)(idx + 1);
        mib_stats_inc_err();
      }
      int g = mib_get(arcs, n, &vals[idx]);
      if (g != SNMP_ERR_NOERROR)
      {
        vals[idx].type = MIB_T_NULL;
        if (reply_err == SNMP_ERR_NOERROR) { reply_err = (uint8_t)g; reply_idx = (uint8_t)(idx+1); }
      }
    }
    else if (pdu_type == BER_TAG_GetNextRequest)
    {
      uint32_t next[BER_OID_MAX_ARCS];
      uint32_t nn = 0;
      int r = mib_get_next(arcs, n, next, &nn, &vals[idx]);
      if (r != SNMP_ERR_NOERROR)
      {
        vals[idx].type = MIB_T_NULL;
        narcs[idx] = n; for (uint32_t k=0;k<n;k++) oids[idx][k]=arcs[k];
        if (reply_err == SNMP_ERR_NOERROR) { reply_err = SNMP_ERR_NOSUCHNAME; reply_idx = (uint8_t)(idx+1); }
      }
      else
      {
        narcs[idx] = nn; for (uint32_t k=0;k<nn;k++) oids[idx][k]=next[k];
      }
    }
    else  /* GetRequest */
    {
      int r = mib_get(arcs, n, &vals[idx]);
      if (r != SNMP_ERR_NOERROR)
      {
        vals[idx].type = MIB_T_NULL;
        if (reply_err == SNMP_ERR_NOERROR) { reply_err = (uint8_t)r; reply_idx = (uint8_t)(idx+1); }
        mib_stats_inc_err();
      }
    }
    nvb++;
  }

  int len = build_response(out, outcap, req_id, pdu_type,
                           reply_err, reply_idx,
                           (const uint32_t(*)[BER_OID_MAX_ARCS])oids,
                           narcs, vals, nvb);
  return len;
}
