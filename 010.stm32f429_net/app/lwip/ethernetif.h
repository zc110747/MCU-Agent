#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__

#include "lwip/err.h"
#include "lwip/netif.h"

/* Network interface name */
#define IFNAME0 's'
#define IFNAME1 't'

/* Exported functions */
err_t ethernetif_init(struct netif *netif);
void ethernet_link_check(struct netif *netif);
void ethernet_link_status_updated(struct netif *netif);

void Error_Handler(void);

#endif /* __ETHERNETIF_H__ */
