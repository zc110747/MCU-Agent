/**
  ******************************************************************************
  * @file    mib.c
  * @brief   Custom MIB implementation (see mib.h).
  *
  *          The node table is the single source of truth. Each entry lists its
  *          full OID (relative to MIB_ROOT), its type, read/write permission,
  *          and accessors that pull from hwinfo / g_netcfg or write to the
  *          shared control path.
  ******************************************************************************
  */
#include "mib.h"
#include "hwinfo.h"     /* shared static/dynamic + set_led/set_beep */
#include "netcfg.h"
#include "stm32f4xx_hal.h"  /* NVIC_SystemReset() */
#include <string.h>
#include <stdio.h>

/* uptime source */
#include "FreeRTOS.h"
#include "task.h"

const uint32_t MIB_ROOT[7] = MIB_ROOT_ARCS;
const uint32_t MIB_ROOT_LEN = 7;

/* ------------------------------------------------------------------ */
/* statistics (32.5)                                                   */
/* ------------------------------------------------------------------ */
static uint32_t s_req;
static uint32_t s_err;
static uint32_t s_last_tick;

void mib_stats_inc_req(void) { s_req++; s_last_tick = xTaskGetTickCount(); }
void mib_stats_inc_err(void) { s_err++; }
uint32_t mib_stats_req(void) { return s_req; }
uint32_t mib_stats_err(void) { return s_err; }
uint32_t mib_stats_last_tick(void) { return s_last_tick; }

/* ------------------------------------------------------------------ */
/* Node table. Each OID is listed *without* the root prefix (1.3.6.1.4.1.32).
 * The first arc is the group (1=system,2=network,3=sensors,4=control,5=stats).
 * ------------------------------------------------------------------ */
typedef struct {
  uint32_t  oid[BER_OID_MAX_ARCS];
  uint32_t  n;            /* number of arcs in oid (relative to root) */
  uint8_t   writable;
  /* get fills val; returns 0 ok, -1 unsupported (shouldn't happen) */
  int     (*get)(mib_val_t *val);
  /* set applies val; returns 0 ok, -1 reject */
  int     (*set)(const mib_val_t *val);
} mib_node_t;

/* ---- accessors ---- */
static int gsys_mcu(mib_val_t *v) {
  hwinfo_static_t s; hwinfo_static_copy(&s);
  v->type = MIB_T_OCTET; v->oct = (const uint8_t*)s.mcu; v->octlen = (uint32_t)strlen(s.mcu);
  return 0;
}
static int gsys_clock(mib_val_t *v) {
  hwinfo_static_t s; hwinfo_static_copy(&s);
  v->type = MIB_T_OCTET; v->oct = (const uint8_t*)s.clock; v->octlen = (uint32_t)strlen(s.clock);
  return 0;
}
static int gsys_tasks(mib_val_t *v) {
  hwinfo_static_t s; hwinfo_static_copy(&s);
  v->type = MIB_T_INTEGER; v->i = (int32_t)s.freertos_tasks; return 0;
}
static int gsys_uptime(mib_val_t *v) {
  v->type = MIB_T_TIMETICKS; v->u = xTaskGetTickCount() / portTICK_PERIOD_MS; /* 1/100s units */
  return 0;
}
static int gnet_ip(mib_val_t *v) {
  static uint8_t ip[4];
  unsigned a,b,c,d;
  if (sscanf(g_netcfg.ip,"%u.%u.%u.%u",&a,&b,&c,&d)!=4)
    a=b=c=d=0;
  ip[0]=(uint8_t)a; ip[1]=(uint8_t)b; ip[2]=(uint8_t)c; ip[3]=(uint8_t)d;
  v->type = MIB_T_IPADDR; v->oct = ip; v->octlen = 4; return 0;
}
static int gnet_mask(mib_val_t *v) {
  static uint8_t ip[4]={0,0,0,0};
  unsigned a,b,c,d;
  if (sscanf(g_netcfg.mask,"%u.%u.%u.%u",&a,&b,&c,&d)!=4) a=b=c=d=0;
  ip[0]=(uint8_t)a; ip[1]=(uint8_t)b; ip[2]=(uint8_t)c; ip[3]=(uint8_t)d;
  v->type = MIB_T_IPADDR; v->oct = ip; v->octlen = 4; return 0;
}
static int gnet_gw(mib_val_t *v) {
  static uint8_t ip[4]={0,0,0,0};
  unsigned a,b,c,d;
  if (sscanf(g_netcfg.gw,"%u.%u.%u.%u",&a,&b,&c,&d)!=4) a=b=c=d=0;
  ip[0]=(uint8_t)a; ip[1]=(uint8_t)b; ip[2]=(uint8_t)c; ip[3]=(uint8_t)d;
  v->type = MIB_T_IPADDR; v->oct = ip; v->octlen = 4; return 0;
}
static int gnet_mac(mib_val_t *v) {
  hwinfo_static_t s; hwinfo_static_copy(&s);
  static char macbuf[NETCFG_MAC_LEN];
  strncpy(macbuf, s.mac, sizeof(macbuf)-1); macbuf[sizeof(macbuf)-1]=0;
  v->type = MIB_T_OCTET; v->oct = (const uint8_t*)macbuf; v->octlen = (uint32_t)strlen(macbuf);
  return 0;
}
static int gsens_lux(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_GAUGE32; v->u = d.lux; return 0;
}
static int gsens_ps(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_GAUGE32; v->u = d.ps; return 0;
}
static int gsens_ir(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_GAUGE32; v->u = d.ir; return 0;
}
static int gsens_ax(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = (int32_t)(d.ax*100); return 0;  /* 1/100 g */
}
static int gsens_ay(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = (int32_t)(d.ay*100); return 0;
}
static int gsens_az(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = (int32_t)(d.az*100); return 0;
}
static int gsens_gx(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = (int32_t)(d.gx*100); return 0;
}
static int gsens_gy(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = (int32_t)(d.gy*100); return 0;
}
static int gsens_gz(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = (int32_t)(d.gz*100); return 0;
}
static int gsens_mx(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = (int32_t)(d.mx*100); return 0;
}
static int gsens_my(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = (int32_t)(d.my*100); return 0;
}
static int gsens_mz(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = (int32_t)(d.mz*100); return 0;
}
static int gsens_valid(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = d.sensor_valid; return 0;
}
static int gctrl_led(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = d.led_on; return 0;
}
static int gctrl_beep(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_INTEGER; v->i = d.beep_on; return 0;
}
static int sctrl_led(const mib_val_t *v) {
  hwinfo_set_led((uint8_t)(v->i ? 1 : 0)); return 0;
}
static int sctrl_beep(const mib_val_t *v) {
  hwinfo_set_beep((uint8_t)(v->i ? 1 : 0)); return 0;
}
/* writable network parameters: persist to EEPROM (same path as web /api/network).
 * Changes take effect after a reset (netif configured once at boot). */
static int snet_ip(const mib_val_t *v) {
  if (v->type != MIB_T_OCTET || v->octlen == 0 || v->octlen >= NETCFG_IP_LEN) return -1;
  char buf[NETCFG_IP_LEN];
  memcpy(buf, v->oct, v->octlen); buf[v->octlen] = '\0';
  strncpy(g_netcfg.ip, buf, sizeof(g_netcfg.ip) - 1);
  g_netcfg.ip[sizeof(g_netcfg.ip) - 1] = '\0';
  netcfg_save(&g_netcfg); return 0;
}
static int snet_mask(const mib_val_t *v) {
  if (v->type != MIB_T_OCTET || v->octlen == 0 || v->octlen >= NETCFG_IP_LEN) return -1;
  char buf[NETCFG_IP_LEN];
  memcpy(buf, v->oct, v->octlen); buf[v->octlen] = '\0';
  strncpy(g_netcfg.mask, buf, sizeof(g_netcfg.mask) - 1);
  g_netcfg.mask[sizeof(g_netcfg.mask) - 1] = '\0';
  netcfg_save(&g_netcfg); return 0;
}
static int snet_gw(const mib_val_t *v) {
  if (v->type != MIB_T_OCTET || v->octlen == 0 || v->octlen >= NETCFG_IP_LEN) return -1;
  char buf[NETCFG_IP_LEN];
  memcpy(buf, v->oct, v->octlen); buf[v->octlen] = '\0';
  strncpy(g_netcfg.gw, buf, sizeof(g_netcfg.gw) - 1);
  g_netcfg.gw[sizeof(g_netcfg.gw) - 1] = '\0';
  netcfg_save(&g_netcfg); return 0;
}
/* system reset: Set=1 triggers a soft reset after persisting config.
 * Get always reports 0 (not in reset state). */
static int gctrl_reset(mib_val_t *v) { v->type = MIB_T_INTEGER; v->i = 0; return 0; }
static int sctrl_reset(const mib_val_t *v) {
  if (v->i != 0) {
    netcfg_save(&g_netcfg);          /* make sure latest params are stored */
    vTaskDelay(pdMS_TO_TICKS(50));   /* let the SNMP response flush */
    NVIC_SystemReset();
  }
  return 0;
}
static int gstat_req(mib_val_t *v) { v->type = MIB_T_COUNTER32; v->u = mib_stats_req(); return 0; }
static int gstat_err(mib_val_t *v) { v->type = MIB_T_COUNTER32; v->u = mib_stats_err(); return 0; }
static int gstat_upd(mib_val_t *v) { v->type = MIB_T_TIMETICKS; v->u = mib_stats_last_tick()/portTICK_PERIOD_MS; return 0; }
/* 32.5.4: I2C bus recovery count — non-zero means the bus jammed at least
 * once and was auto-healed by hwinfo_task (instead of freezing data at 0). */
static int gstat_i2c(mib_val_t *v) {
  hwinfo_dynamic_t d; hwinfo_dynamic_copy(&d);
  v->type = MIB_T_COUNTER32; v->u = d.i2c_recover; return 0;
}

/* Helper macro to declare a node with full relative OID. */
#define NODE(...)  .oid = { __VA_ARGS__ }, .n = (sizeof((uint32_t[]){__VA_ARGS__})/sizeof(uint32_t))

static const mib_node_t MIB[] = {
  /* 32.1 system */
  { NODE(1,1), 0, gsys_mcu },   /* sysMcu        OCTET STRING */
  { NODE(1,2), 0, gsys_clock }, /* sysClock      OCTET STRING */
  { NODE(1,3), 0, gsys_tasks }, /* sysTasks      INTEGER */
  { NODE(1,4), 0, gsys_uptime },/* sysUptime     TimeTicks */
  /* 32.2 network (ip/mask/gw writable, persist via netcfg_save) */
  { NODE(2,1), 1, gnet_ip,  snet_ip },   /* netIp     IpAddress (rw) */
  { NODE(2,2), 1, gnet_mask, snet_mask },/* netMask   IpAddress (rw) */
  { NODE(2,3), 1, gnet_gw,  snet_gw },   /* netGw     IpAddress (rw) */
  { NODE(2,4), 0, gnet_mac },            /* netMac    OCTET STRING (ro) */
  /* 32.3 sensors */
  { NODE(3,1), 0, gsens_lux },  /* sensLux       Gauge32 */
  { NODE(3,2), 0, gsens_ps },   /* sensPs        Gauge32 */
  { NODE(3,3), 0, gsens_ir },   /* sensIr        Gauge32 */
  { NODE(3,4), 0, gsens_ax },   /* sensAx        INTEGER (1/100 g) */
  { NODE(3,5), 0, gsens_ay },
  { NODE(3,6), 0, gsens_az },
  { NODE(3,7), 0, gsens_gx },   /* sensGx        INTEGER (1/100 dps) */
  { NODE(3,8), 0, gsens_gy },
  { NODE(3,9), 0, gsens_gz },
  { NODE(3,10),0, gsens_mx },   /* sensMx        INTEGER (1/100 uT) */
  { NODE(3,11),0, gsens_my },
  { NODE(3,12),0, gsens_mz },
  { NODE(3,13),0, gsens_valid },/* sensValid     INTEGER (0/1) */
  /* 32.4 control (writable) */
  { NODE(4,1), 1, gctrl_led,  sctrl_led },  /* ctrlLed   INTEGER 0/1 */
  { NODE(4,2), 1, gctrl_beep, sctrl_beep }, /* ctrlBeep  INTEGER 0/1 */
  { NODE(4,3), 1, gctrl_reset, sctrl_reset },/* ctrlReset INTEGER 0/1 (Set=1 -> soft reset) */
  /* 32.5 stats */
  { NODE(5,1), 0, gstat_req },  /* statReq       Counter32 */
  { NODE(5,2), 0, gstat_err },  /* statErr       Counter32 */
  { NODE(5,3), 0, gstat_upd },  /* statLastUpd   TimeTicks */
  { NODE(5,4), 0, gstat_i2c },  /* statI2cRecover Counter32 */
};
static const uint32_t MIB_COUNT = sizeof(MIB) / sizeof(MIB[0]);

/* Compare two OIDs (relative arcs). Returns <0,0,>0. */
static int oid_cmp(const uint32_t *a, uint32_t na, const uint32_t *b, uint32_t nb)
{
  uint32_t m = (na < nb) ? na : nb;
  for (uint32_t i = 0; i < m; i++)
  {
    if (a[i] != b[i]) return (a[i] < b[i]) ? -1 : 1;
  }
  if (na != nb) return (na < nb) ? -1 : 1;
  return 0;
}

/* Find exact node by relative OID. Returns index or -1. */
static int find_node(const uint32_t *oid, uint32_t n)
{
  for (uint32_t i = 0; i < MIB_COUNT; i++)
  {
    if (oid_cmp(oid, n, MIB[i].oid, MIB[i].n) == 0)
      return (int)i;
  }
  return -1;
}

/* ------------------------------------------------------------------ */
/* Public dispatch                                                     */
/* ------------------------------------------------------------------ */
int mib_get(const uint32_t *arcs, uint32_t narcs, mib_val_t *val)
{
  /* arcs includes the root (1.3.6.1.4.1.32). Strip it. */
  if (narcs <= MIB_ROOT_LEN) return SNMP_ERR_NOSUCHNAME;
  const uint32_t *rel = arcs + MIB_ROOT_LEN;
  uint32_t nrel = narcs - MIB_ROOT_LEN;
  int idx = find_node(rel, nrel);
  if (idx < 0) return SNMP_ERR_NOSUCHNAME;
  memset(val, 0, sizeof(*val));
  MIB[idx].get(val);
  return SNMP_ERR_NOERROR;
}

int mib_set(const uint32_t *arcs, uint32_t narcs, const mib_val_t *val)
{
  if (narcs <= MIB_ROOT_LEN) return SNMP_ERR_NOSUCHNAME;
  const uint32_t *rel = arcs + MIB_ROOT_LEN;
  uint32_t nrel = narcs - MIB_ROOT_LEN;
  int idx = find_node(rel, nrel);
  if (idx < 0) return SNMP_ERR_NOSUCHNAME;
  if (!MIB[idx].writable || MIB[idx].set == NULL) return SNMP_ERR_READONLY;
  if (MIB[idx].set(val) != 0) return SNMP_ERR_BADVALUE;
  return SNMP_ERR_NOERROR;
}

int mib_get_next(const uint32_t *arcs, uint32_t narcs,
                 uint32_t *out, uint32_t *outn, mib_val_t *val)
{
  const uint32_t *rel;
  uint32_t nrel;
  if (narcs <= MIB_ROOT_LEN) { rel = arcs + MIB_ROOT_LEN; nrel = narcs - MIB_ROOT_LEN; }
  else { rel = arcs + MIB_ROOT_LEN; nrel = narcs - MIB_ROOT_LEN; }

  int best = -1;
  for (uint32_t i = 0; i < MIB_COUNT; i++)
  {
    int c = oid_cmp(MIB[i].oid, MIB[i].n, rel, nrel);
    if (c <= 0) continue;                       /* must be strictly greater */
    if (best < 0 ||
        oid_cmp(MIB[i].oid, MIB[i].n, MIB[best].oid, MIB[best].n) < 0)
      best = (int)i;
  }
  if (best < 0) return SNMP_ERR_NOSUCHNAME;

  *outn = MIB_ROOT_LEN + MIB[best].n;
  for (uint32_t i = 0; i < MIB_ROOT_LEN; i++) out[i] = MIB_ROOT[i];
  for (uint32_t i = 0; i < MIB[best].n; i++) out[MIB_ROOT_LEN + i] = MIB[best].oid[i];

  memset(val, 0, sizeof(*val));
  MIB[best].get(val);
  return SNMP_ERR_NOERROR;
}

/* ------------------------------------------------------------------ */
/* VarBind encoder                                                     */
/* ------------------------------------------------------------------ */
int mib_encode_varbind(ber_enc_t *e, const uint32_t *arcs, uint32_t narcs,
                       const mib_val_t *val)
{
  uint32_t marker;
  if (ber_enc_begin(e, BER_TAG_SEQUENCE, &marker) != 0) return -1;
  if (ber_enc_oid(e, arcs, narcs) != 0) return -1;

  switch (val->type)
  {
    case MIB_T_INTEGER:  ber_enc_integer(e, val->i); break;
    case MIB_T_COUNTER32:ber_enc_u32_app(e, BER_TAG_COUNTER32, val->u); break;
    case MIB_T_GAUGE32:  ber_enc_u32_app(e, BER_TAG_GAUGE32, val->u); break;
    case MIB_T_TIMETICKS:ber_enc_u32_app(e, BER_TAG_TIMETICKS, val->u); break;
    case MIB_T_OCTET:    ber_enc_octet(e, val->oct, val->octlen); break;
    case MIB_T_IPADDR:   ber_enc_ip(e, val->oct); break;
    case MIB_T_NULL:     ber_enc_null(e); break;
    default:             ber_enc_null(e); break;
  }
  ber_enc_end(e, marker);
  return 0;
}
