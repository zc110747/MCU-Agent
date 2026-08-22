#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__

#include "lwip/err.h"
#include "lwip/netif.h"

/* Network interface name */
#define IFNAME0 's'
#define IFNAME1 't'

/* Set by the ETH RX ISR callback; polled by the main loop */
extern volatile uint8_t eth_rx_ready;

/* Exported functions */
err_t ethernetif_init(struct netif *netif);
void ethernetif_input(struct netif *netif);
void ethernet_link_check(struct netif *netif);

void Error_Handler(void);
u32_t sys_now(void);

#endif /* __ETHERNETIF_H__ */
