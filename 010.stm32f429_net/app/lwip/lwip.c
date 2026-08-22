/**
  ******************************************************************************
  * @file    lwip.c
  * @brief   LwIP initialization (FreeRTOS / tcpip_thread mode).
  ******************************************************************************
  */
#include "lwip.h"
#include "lwip/init.h"
#include "lwip/tcpip.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

struct netif gnetif;
static ip4_addr_t ipaddr;
static ip4_addr_t netmask;
static ip4_addr_t gw;

/* ---- Ethernet link monitor task (checks LAN8720 PHY link every 500 ms) ---- */
static void ethernet_link_thread(void *arg)
{
  struct netif *netif = (struct netif *)arg;

  for (;;)
  {
    ethernet_link_check(netif);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

/**
  * @brief  LwIP initialization with FreeRTOS.
  *         Creates the tcpip_thread (tcpip_init), registers the netif with
  *         tcpip_input() as its input function, and spawns the link task.
  */
void MX_LWIP_Init(void)
{
  /* Start the LwIP core thread (tcpip_thread): owns the protocol stack */
  tcpip_init(NULL, NULL);

  /* FreeRTOS V11 port quirk: xTaskCreate() inside tcpip_init() calls
   * vPortEnterCritical() which sets BASEPRI even before the scheduler runs,
   * but vPortExitCritical() only restores it once the scheduler is running.
   * The leftover BASEPRI=0x50 silently blocks timer IRQs (TIM7 HAL tick)
   * so HAL_Delay() below would hang.  Restore the interrupt mask here. */
  __set_BASEPRI(0);
  __enable_irq();

  /* IP addresses initialization */
  IP4_ADDR(&ipaddr, LWIP_IP_ADDR0, LWIP_IP_ADDR1, LWIP_IP_ADDR2, LWIP_IP_ADDR3);
  IP4_ADDR(&netmask, LWIP_NETMASK0, LWIP_NETMASK1, LWIP_NETMASK2, LWIP_NETMASK3);
  IP4_ADDR(&gw, LWIP_GW_ADDR0, LWIP_GW_ADDR1, LWIP_GW_ADDR2, LWIP_GW_ADDR3);

  /* Add the network interface; input function hands pbufs to tcpip_thread */
  netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &tcpip_input);

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

  netif_set_link_callback(&gnetif, ethernet_link_status_updated);

  /* Ethernet link monitor task */
  xTaskCreate(ethernet_link_thread, "EthLink", 256, &gnetif, tskIDLE_PRIORITY + 2, NULL);
}

u8_t netif_link_up(void)
{
  return netif_is_link_up(&gnetif);
}
