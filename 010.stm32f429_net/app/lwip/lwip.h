#ifndef __MX_LWIP_H
#define __MX_LWIP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/opt.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "netif/etharp.h"
#include "netif/ethernet.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "ethernetif.h"

/* Static IPv4 configuration (see task requirements) */
#define LWIP_IP_ADDR0   192
#define LWIP_IP_ADDR1   168
#define LWIP_IP_ADDR2   10
#define LWIP_IP_ADDR3   99

#define LWIP_NETMASK0   255
#define LWIP_NETMASK1   255
#define LWIP_NETMASK2   255
#define LWIP_NETMASK3   0

#define LWIP_GW_ADDR0   192
#define LWIP_GW_ADDR1   168
#define LWIP_GW_ADDR2   10
#define LWIP_GW_ADDR3   1

/* Global network interface */
extern struct netif gnetif;

void MX_LWIP_Init(void);
void MX_LWIP_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* __MX_LWIP_H */
