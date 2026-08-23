/**
  ******************************************************************************
  * @file    mib.h
  * @brief   Custom SNMP MIB for the STM32F429 NET demo.
  *
  *          Root enterprise OID = 1.3.6.1.4.1.32   (32 = LAN-local placeholder)
  *
  *          Node layout (mirrors the web /api surface so the PC client and the
  *          web page show the same data):
  *            32.1   system      (MCU, clock, FreeRTOS tasks, uptime)
 *            32.2   network     (ip, mask, gw writable, mac read-only)
 *            32.3   sensors     (AP3216C light/ps/ir, MPU9250 9-axis)
 *            32.4   control     (led, beep, reset - read/write, mirrors /api/control)
  *            32.5   stats       (snmp requests, errors, last update tick)
  *
  *          The agent calls mib_get()/mib_set() which dispatch by OID to the
  *          per-node accessor. Values are pulled from the shared hwinfo layer
  *          (app/hwinfo.c) and g_netcfg; control writes call hwinfo_set_led()
  *          / hwinfo_set_beep().
  ******************************************************************************
  */
#ifndef __MIB_H__
#define __MIB_H__

#include <stdint.h>
#include "ber.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SNMP error-status codes (RFC 1905). */
#define SNMP_ERR_NOERROR           0
#define SNMP_ERR_TOO_BIG           1
#define SNMP_ERR_NOSUCHNAME        2
#define SNMP_ERR_BADVALUE          3
#define SNMP_ERR_READONLY          4
#define SNMP_ERR_GENERR            5

/* Value types the MIB accessor produces (drive BER encoding). */
typedef enum {
  MIB_T_INTEGER = 0,     /* int32 */
  MIB_T_COUNTER32,       /* uint32 app 0x41 */
  MIB_T_GAUGE32,         /* uint32 app 0x42 */
  MIB_T_TIMETICKS,       /* uint32 app 0x43 */
  MIB_T_OCTET,           /* string/bytes */
  MIB_T_IPADDR,          /* 4 bytes */
  MIB_T_NULL             /* for error placeholders */
} mib_type_t;

/* A value produced by a MIB get. */
typedef struct {
  mib_type_t type;
  int32_t    i;                  /* INTEGER */
  uint32_t   u;                  /* counter/gauge/timeticks */
  const uint8_t * oct;           /* OCTET STRING / IP bytes */
  uint32_t   octlen;
} mib_val_t;

/* OID root: 1.3.6.1.4.1.32 */
#define MIB_ROOT_ARCS  {1,3,6,1,4,1,32}
extern const uint32_t MIB_ROOT[7];
extern const uint32_t MIB_ROOT_LEN;

/* ---- Agent statistics (32.5) backing counters ---- */
void     mib_stats_inc_req(void);
void     mib_stats_inc_err(void);
uint32_t mib_stats_req(void);
uint32_t mib_stats_err(void);
uint32_t mib_stats_last_tick(void);   /* xTaskGetTickCount at last request */

/* ---- Core dispatch ----
 * get:  fill *val for the OID in arcs[0..narcs-1]. Return SNMP error-status.
 * set:  write *val into the OID (only writable nodes). Return error-status.
 *       For GetNext, the agent enumerates the static node table and picks
 *       the lexically-next OID. */
int  mib_get(const uint32_t *arcs, uint32_t narcs, mib_val_t *val);
int  mib_set(const uint32_t *arcs, uint32_t narcs, const mib_val_t *val);

/* GetNext support: given an OID, find the next registered OID that is greater
 * (lexical arc compare). Writes the next OID into out[] and its value.
 * Returns SNMP_ERR_NOSUCHNAME if no successor exists. */
int  mib_get_next(const uint32_t *arcs, uint32_t narcs,
                  uint32_t *out, uint32_t *outn, mib_val_t *val);

/* Encode a VarBind (OID + value) into the encoder at the current position.
 * val may be a real value or an error placeholder (type MIB_T_NULL with a
 * noSuchName marker handled by the caller). */
int  mib_encode_varbind(ber_enc_t *e, const uint32_t *arcs, uint32_t narcs,
                        const mib_val_t *val);

#ifdef __cplusplus
}
#endif

#endif /* __MIB_H__ */
