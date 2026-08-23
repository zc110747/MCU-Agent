/**
  ******************************************************************************
  * @file    snmp_agent.c
  * @brief   SNMP v2c Agent (UDP 161) implemented in a dedicated FreeRTOS task.
  *
  *          Protocol handled: SNMP v2c (community "public" by default).
  *          PDU types: GetRequest(0xA0), GetNextRequest(0xA1), SetRequest(0xA3).
  *
  *          Packet structure handled:
  *            SEQUENCE {                         -- SNMP message
  *              INTEGER version (1 = v2c)
  *              OCTET STRING community
  *              PDU ::= SEQUENCE {              -- 0xA0/0xA1/0xA3
  *                INTEGER request-id
  *                INTEGER error-status
  *                INTEGER error-index
  *                VarBindList ::= SEQUENCE OF {
  *                  SEQUENCE { OID, value }
  *                }
  *              }
  *            }
  *
  *          The MIB dispatch (mib_get/mib_set/mib_get_next) lives in mib.c.
  *          All encoding uses the minimal BER codec (ber.c). No heap use in the
  *          hot path: the response is built into a stack buffer.
  ******************************************************************************
  */
#include "snmp_agent.h"
#include "mib.h"
#include "ber.h"
#include "log.h"

#include "lwip/api.h"
#include "lwip/ip_addr.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>
#include <stdio.h>

/* SNMP message decode/dispatch lives in snmp_msg.c (handle_message). */
#include "snmp_msg.h"

#define SNMP_BUF   1024   /* max SNMP message size we handle */

/* ------------------------------------------------------------------ */
/* Agent task                                                          */
/* ------------------------------------------------------------------ */
#define SNMP_STACK   1024
#define SNMP_PRIO    3

static void snmp_agent_thread(void *arg)
{
  (void)arg;
  struct netconn *conn = netconn_new(NETCONN_UDP);
  if (conn == NULL)
  {
    PRINT_LOG("SNMP: netconn_new failed\r\n");
    vTaskDelete(NULL);
    return;
  }
  err_t err = netconn_bind(conn, IP_ADDR_ANY, SNMP_AGENT_PORT);
  if (err != ERR_OK)
  {
    PRINT_LOG("SNMP: bind %d failed (%d)\r\n", SNMP_AGENT_PORT, (int)err);
    netconn_delete(conn);
    vTaskDelete(NULL);
    return;
  }
  PRINT_LOG("SNMP agent: listening on UDP port %d (v2c, community 'public')\r\n", SNMP_AGENT_PORT);

  for (;;)
  {
    struct netbuf *nb = NULL;
    err = netconn_recv(conn, &nb);
    if (err != ERR_OK || nb == NULL)
      continue;

    uint8_t in[SNMP_BUF];
    uint8_t out[SNMP_BUF];
    uint16_t inlen = 0;
    uint16_t outlen = 0;

    /* copy payload from the netbuf chain */
    if (nb->p != NULL)
    {
      struct pbuf *p = nb->p;
      uint16_t room = SNMP_BUF;
      while (p != NULL && room > 0)
      {
        uint16_t take = (p->len > room) ? room : p->len;
        memcpy(in + inlen, p->payload, take);
        inlen += take;
        room  -= take;
        p = p->next;
      }
    }

    if (inlen > 0)
    {
      int r = handle_message(in, inlen, out, SNMP_BUF);
      if (r > 0)
      {
        outlen = (uint16_t)r;
        /* Send the response back to the same peer (addr/port captured in nb).
         * Use a fresh netbuf that references the encoded response (zero-copy). */
        struct netbuf *rb = netbuf_new();
        if (rb != NULL)
        {
          netbuf_ref(rb, out, outlen);   /* zero-copy reference */
          err_t se = netconn_sendto(conn, rb, &nb->addr, nb->port);
          (void)se;
          netbuf_delete(rb);
        }
      }
    }
    netbuf_delete(nb);
  }
}

int snmp_agent_init(void)
{
  if (xTaskCreate(snmp_agent_thread, "snmpd", SNMP_STACK, NULL,
                  SNMP_PRIO, NULL) != pdPASS)
  {
    PRINT_LOG("SNMP: task create failed\r\n");
    return pdFAIL;
  }
  return pdPASS;
}
