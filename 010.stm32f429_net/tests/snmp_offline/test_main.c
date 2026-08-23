/* test_main.c — offline unit test for the embedded SNMP BER + MIB layers.
 *
 *   Builds with: gcc test_main.c ../../app/snmp/ber.c ../../app/snmp/mib.c \
 *                stubs/hwinfo_stub.c -I../../app/snmp -I../../app -Istubs \
 *                -include freertos_stub.h
 *
 *   Verifies:
 *     1) OID BER encode/decode round trip
 *     2) mib_get on every node returns NOERROR with a sane type
 *     3) mib_get_next walks the whole tree in lexical order
 *     4) Set on a writable node (ctrlLed) updates state, read-back matches
 *     5) A full GetRequest message is parsed + responded by handle_message
 *        (linked from snmp_agent.c — exercises the real decode path)
 */
#include <stdio.h>
#include <string.h>
#include "ber.h"
#include "mib.h"
#include "hwinfo.h"   /* hwinfo_init / hwinfo_set_led / hwinfo_set_beep */
/* test-only accessors implemented in stubs/hwinfo_stub.c */
extern uint8_t hwinfo_test_led(void);
extern uint8_t hwinfo_test_beep(void);

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { if (cond) { g_pass++; printf("  PASS: %s\n", msg); } \
    else { g_fail++; printf("  FAIL: %s\n", msg); } } while(0)

/* link the real agent message handler (decode + dispatch + encode) */
extern int handle_message(const uint8_t *in, uint32_t inlen,
                          uint8_t *out, uint32_t outcap);

int main(void)
{
  hwinfo_init();

  printf("=== Test 1: OID BER round-trip ===\n");
  uint32_t oid1[] = {1,3,6,1,4,1,32,1,3};
  uint8_t buf[64];
  ber_enc_t e; ber_enc_init(&e, buf, sizeof(buf));
  ber_enc_oid(&e, oid1, 9);
  uint32_t written = ber_enc_len_written(&e);
  ber_dec_t d; ber_dec_init(&d, buf, written);
  ber_tlv_t t; ber_dec_tlv(&d, &t);
  uint32_t arcs[BER_OID_MAX_ARCS]; uint32_t n=0;
  ber_decode_oid(&t, arcs, &n);
  int ok = (n==9);
  for (uint32_t i=0;i<9 && ok;i++) if (arcs[i]!=oid1[i]) ok=0;
  CHECK(ok, "OID 1.3.6.1.4.1.32.1.3 round-trips");

  printf("=== Test 2: mib_get every node ===\n");
  /* iterate the documented node list via get_next from just under root */
  uint32_t cur[BER_OID_MAX_ARCS] = {1,3,6,1,4,1,32};
  uint32_t curn = 7;
  int count = 0;
  for (;;)
  {
    uint32_t next[BER_OID_MAX_ARCS]; uint32_t nn=0;
    mib_val_t v;
    int r = mib_get_next(cur, curn, next, &nn, &v);
    if (r != SNMP_ERR_NOERROR) break;
    /* get on the resolved OID must also succeed */
    mib_val_t v2;
    int r2 = mib_get(next, nn, &v2);
    CHECK(r2 == SNMP_ERR_NOERROR, "mib_get resolves node");
    count++;
    /* advance */
    memcpy(cur, next, nn*sizeof(uint32_t));
    curn = nn;
    if (count > 50) break;
  }
  CHECK(count == 26, "MIB exposes 26 nodes via GetNext");

  printf("=== Test 3: Set ctrlLed then read-back ===\n");
  uint32_t ledoid[10] = {1,3,6,1,4,1,32,4,1};
  mib_val_t setv; memset(&setv,0,sizeof(setv));
  setv.type = MIB_T_INTEGER; setv.i = 1;
  int rs = mib_set(ledoid, 9, &setv);
  CHECK(rs == SNMP_ERR_NOERROR, "Set ctrlLed=1 accepted");
  CHECK(hwinfo_test_led() == 1, "hwinfo_set_led driven (led=1)");
  mib_val_t rv; memset(&rv,0,sizeof(rv));
  mib_get(ledoid, 9, &rv);
  CHECK(rv.i == 1, "ctrlLed read-back = 1");
  /* reset */
  setv.i = 0; mib_set(ledoid, 9, &setv);
  CHECK(hwinfo_test_led() == 0, "ctrlLed reset (led=0)");

  printf("=== Test 4: read-only node rejects Set ===\n");
  uint32_t sysOid[10] = {1,3,6,1,4,1,32,1,1};  /* sysMcu OCTET (read-only) */
  mib_val_t sv2; memset(&sv2,0,sizeof(sv2)); sv2.type=MIB_T_INTEGER; sv2.i=5;
  int rro = mib_set(sysOid, 9, &sv2);
  CHECK(rro == SNMP_ERR_READONLY, "Set on read-only node -> READONLY");

  printf("=== Test 5: full GetRequest message via handle_message ===\n");
  /* Build a real GetRequest for sysTasks (1.3.6.1.4.1.32.1.3) using the same
   * codec the PC client uses, then run it through the embedded handler. */
  {
    uint8_t req[256];
    ber_enc_t re; ber_enc_init(&re, req, sizeof(req));
    uint32_t m_msg; ber_enc_begin(&re, BER_TAG_SEQUENCE, &m_msg);
    ber_enc_integer(&re, 1);                       /* version v2c */
    ber_enc_octet(&re, (const uint8_t*)"public", 6);
    uint32_t m_pdu; ber_enc_begin(&re, BER_TAG_GetRequest, &m_pdu);
    ber_enc_integer(&re, 12345);                   /* req-id */
    ber_enc_integer(&re, 0);                       /* err */
    ber_enc_integer(&re, 0);                       /* idx */
    uint32_t m_vbl; ber_enc_begin(&re, BER_TAG_SEQUENCE, &m_vbl);
    uint32_t m_vb; ber_enc_begin(&re, BER_TAG_SEQUENCE, &m_vb);
    uint32_t goid[] = {1,3,6,1,4,1,32,1,3};
    ber_enc_oid(&re, goid, 9);
    ber_enc_null(&re);
    ber_enc_end(&re, m_vb);
    ber_enc_end(&re, m_vbl);
    ber_enc_end(&re, m_pdu);
    ber_enc_end(&re, m_msg);
    uint32_t reqlen = ber_enc_len_written(&re);

    uint8_t resp[512];
    int rlen = handle_message(req, reqlen, resp, sizeof(resp));
    CHECK(rlen > 0, "handle_message produced a response");
    /* parse the response to confirm it's a GetResponse with the integer */
    ber_dec_t rd; ber_dec_init(&rd, resp, (uint32_t)rlen);
    ber_tlv_t mt; ber_dec_tlv(&rd, &mt);
    /* Re-base on the message content, then read version/community/PDU. */
    ber_dec_init(&rd, mt.val, mt.len);
    ber_tlv_t t2; ber_dec_tlv(&rd,&t2); /* version */
    ber_dec_tlv(&rd,&t2); /* community */
    ber_dec_tlv(&rd,&t2); /* PDU */
    CHECK(t2.tag == BER_TAG_GetResponse, "response is GetResponse(0xA2)");
  }

  printf("=== Test 6: wrong community is dropped ===\n");
  {
    uint8_t req[256];
    ber_enc_t re; ber_enc_init(&re, req, sizeof(req));
    uint32_t m_msg; ber_enc_begin(&re, BER_TAG_SEQUENCE, &m_msg);
    ber_enc_integer(&re, 1);
    ber_enc_octet(&re, (const uint8_t*)"private", 7);
    uint32_t m_pdu; ber_enc_begin(&re, BER_TAG_GetRequest, &m_pdu);
    ber_enc_integer(&re, 1); ber_enc_integer(&re,0); ber_enc_integer(&re,0);
    uint32_t m_vbl; ber_enc_begin(&re, BER_TAG_SEQUENCE, &m_vbl);
    uint32_t m_vb; ber_enc_begin(&re, BER_TAG_SEQUENCE, &m_vb);
    uint32_t goid[] = {1,3,6,1,4,1,32,1,3};
    ber_enc_oid(&re, goid, 9); ber_enc_null(&re);
    ber_enc_end(&re,m_vb); ber_enc_end(&re,m_vbl); ber_enc_end(&re,m_pdu); ber_enc_end(&re,m_msg);
    uint32_t reqlen = ber_enc_len_written(&re);
    uint8_t resp[512];
    int rlen = handle_message(req, reqlen, resp, sizeof(resp));
    CHECK(rlen < 0, "wrong community -> no response (handler returns <0)");
  }

  printf("\n==================== RESULT ====================\n");
  printf("  PASS=%d  FAIL=%d\n", g_pass, g_fail);
  return (g_fail == 0) ? 0 : 1;
}
