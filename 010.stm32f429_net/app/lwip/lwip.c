/**
  ******************************************************************************
  * @file    lwip.c
  * @brief   LwIP initialization (NO_SYS / bare-metal).
  ******************************************************************************
  */
#include "lwip.h"
#include "lwip/init.h"
#include "netif/ethernet.h"
#include <string.h>
#include <stdio.h>

struct netif gnetif;
static ip4_addr_t ipaddr;
static ip4_addr_t netmask;
static ip4_addr_t gw;

/**
  * @brief  LwIP initialization function (NO_SYS).
  */
void MX_LWIP_Init(void)
{
  /* Initilialize the LwIP stack */
  lwip_init();

  /* IP addresses initialization */
  IP4_ADDR(&ipaddr, LWIP_IP_ADDR0, LWIP_IP_ADDR1, LWIP_IP_ADDR2, LWIP_IP_ADDR3);
  IP4_ADDR(&netmask, LWIP_NETMASK0, LWIP_NETMASK1, LWIP_NETMASK2, LWIP_NETMASK3);
  IP4_ADDR(&gw, LWIP_GW_ADDR0, LWIP_GW_ADDR1, LWIP_GW_ADDR2, LWIP_GW_ADDR3);

  /* Add the network interface (IPv4) with the raw ethernet input function */
  netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &ethernet_input);

  /* Register the default network interface */
  netif_set_default(&gnetif);

  if (netif_is_link_up(&gnetif))
  {
    netif_set_up(&gnetif);
  }
  else
  {
    netif_set_down(&gnetif);
  }
}

/**
  * @brief  Called periodically from the main loop: process RX packets,
  *         monitor link status, and run LwIP timeouts.
  */
void MX_LWIP_Process(void)
{
  if (eth_rx_ready)
  {
    ethernetif_input(&gnetif);
  }

  ethernet_link_check(&gnetif);

  sys_check_timeouts();
}
