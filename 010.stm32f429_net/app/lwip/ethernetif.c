/**
  ******************************************************************************
  * @file    ethernetif.c
  * @brief   Ethernet interface driver for STM32F429 + LAN8720 (LwIP + FreeRTOS).
  *          RX pbufs are handed to the tcpip_thread directly from the ETH ISR
  *          via tcpip_input(); link state is polled by a dedicated task.
  ******************************************************************************
  */
#include "stm32f4xx_hal.h"
#include "lwip/opt.h"
#include "lwip/timeouts.h"
#include "lwip/tcpip.h"
#include "lwip/memp.h"
#include "netif/ethernet.h"
#include "netif/etharp.h"
#include "lwip/ethip6.h"
#include "ethernetif.h"
#include "lan8720.h"
#include "netcfg.h"
#include <string.h>
#include <stdio.h>

/* Network interface name */
#define IFNAME0 's'
#define IFNAME1 't'

/* The global netif (defined in lwip.c) */
extern struct netif gnetif;

/* ETH settings */
#define ETH_DMA_TRANSMIT_TIMEOUT   ( 20U )
#define ETH_RX_BUFFER_CNT          20U

/* Private variables ---------------------------------------------------------*/
typedef enum
{
  RX_ALLOC_OK    = 0x00,
  RX_ALLOC_ERROR = 0x01
} RxAllocStatusTypeDef;

typedef struct
{
  struct pbuf_custom pbuf_custom;
  uint8_t buff[(ETH_RX_BUF_SIZE + 31) & ~31U] __ALIGNED(32);
} RxBuff_t;

LWIP_MEMPOOL_DECLARE(RX_POOL, ETH_RX_BUFFER_CNT, sizeof(RxBuff_t), "Zero-copy RX PBUF pool");

static uint8_t RxAllocStatus;

ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT];  /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT];  /* Ethernet Tx DMA Descriptors */

ETH_HandleTypeDef heth;
ETH_TxPacketConfig TxConfig;

/* PHY IO context */
static int32_t ETH_PHY_IO_Init(void);
static int32_t ETH_PHY_IO_DeInit(void);
static int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal);
static int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal);
static int32_t ETH_PHY_IO_GetTick(void);
static void mac_dma_init(void);

lan8720_Object_t LAN8720;
lan8720_IOCtx_t  LAN8720_IOCtx = {
  ETH_PHY_IO_Init,
  ETH_PHY_IO_DeInit,
  ETH_PHY_IO_ReadReg,
  ETH_PHY_IO_WriteReg,
  ETH_PHY_IO_GetTick
};

/* Private function prototypes -----------------------------------------------*/
void pbuf_free_custom(struct pbuf *p);

/* Callbacks used by the HAL ETH driver (new API) */
void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *handlerEth);
void HAL_ETH_RxAllocateCallback(uint8_t **buff);
void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length);
void HAL_ETH_TxFreeCallback(uint32_t *buff);

/* ETH MSP */
void HAL_ETH_MspInit(ETH_HandleTypeDef *ethHandle);
void HAL_ETH_MspDeInit(ETH_HandleTypeDef *ethHandle);

/* Link monitoring state */
static uint8_t link_up_prev = 0;

/* USER CODE END 0 */

/*******************************************************************************
                       LL Driver Interface (LwIP stack --> ETH)
*******************************************************************************/
static void low_level_init(struct netif *netif)
{
  HAL_StatusTypeDef hal_eth_init_status = HAL_OK;
  uint32_t duplex = 0U, speed = 0U;
  int32_t PHYLinkState = 0;
  ETH_MACConfigTypeDef MACConf = {0};

  uint8_t MACAddr[6];

  heth.Instance = ETH;
  /* MAC from runtime config (defaults or SD netcfg.ini, settable via API) */
  {
    unsigned macb[6];
    if (sscanf(g_netcfg.mac, "%x:%x:%x:%x:%x:%x",
               &macb[0], &macb[1], &macb[2], &macb[3], &macb[4], &macb[5]) == 6)
    {
      for (int i = 0; i < 6; i++)
      {
        MACAddr[i] = (uint8_t)macb[i];
      }
    }
    else
    {
      MACAddr[0] = 0x00; MACAddr[1] = 0x80; MACAddr[2] = 0xE1;
      MACAddr[3] = 0x00; MACAddr[4] = 0x00; MACAddr[5] = 0x00;
    }
  }
  heth.Init.MACAddr = &MACAddr[0];
  heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;
  heth.Init.RxBuffLen = ETH_RX_BUF_SIZE;

  hal_eth_init_status = HAL_ETH_Init(&heth);

  mac_dma_init();

  memset(&TxConfig, 0, sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  /* Must track ETH_USE_HW_CHECKSUM in lwipopts.h -- see the long comment
   * there.  HW insertion corrupts the L4 checksum of IP fragments, so the
   * default build lets lwIP compute checksums and disables MAC insertion. */
#if ETH_USE_HW_CHECKSUM
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
#else
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_DISABLE;
#endif
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;

  /* Initialize the RX POOL (must be done before HAL_ETH_Start_IT fills descriptors) */
  LWIP_MEMPOOL_INIT(RX_POOL);

#if LWIP_ARP || LWIP_ETHERNET
  /* set MAC hardware address length */
  netif->hwaddr_len = ETH_HWADDR_LEN;

  /* set MAC hardware address */
  netif->hwaddr[0] = heth.Init.MACAddr[0];
  netif->hwaddr[1] = heth.Init.MACAddr[1];
  netif->hwaddr[2] = heth.Init.MACAddr[2];
  netif->hwaddr[3] = heth.Init.MACAddr[3];
  netif->hwaddr[4] = heth.Init.MACAddr[4];
  netif->hwaddr[5] = heth.Init.MACAddr[5];

  /* maximum transfer unit */
  netif->mtu = ETH_MAX_PAYLOAD;

  /* Accept broadcast address and ARP traffic */
#if LWIP_ARP
  netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
#else
  netif->flags |= NETIF_FLAG_BROADCAST;
#endif /* LWIP_ARP */

  /* Set PHY IO functions */
  LAN8720_RegisterBusIO(&LAN8720, &LAN8720_IOCtx);

  /* Initialize the LAN8720 PHY */
  LAN8720_Init(&LAN8720);

  if (hal_eth_init_status == HAL_OK)
  {
    PHYLinkState = LAN8720_GetLinkState(&LAN8720);

    if (PHYLinkState <= LAN8720_STATUS_LINK_DOWN)
    {
      netif_set_link_down(netif);
      netif_set_down(netif);
    }
    else
    {
      switch (PHYLinkState)
      {
        case LAN8720_STATUS_100MBITS_FULLDUPLEX:
          duplex = ETH_FULLDUPLEX_MODE;
          speed = ETH_SPEED_100M;
          break;
        case LAN8720_STATUS_100MBITS_HALFDUPLEX:
          duplex = ETH_HALFDUPLEX_MODE;
          speed = ETH_SPEED_100M;
          break;
        case LAN8720_STATUS_10MBITS_FULLDUPLEX:
          duplex = ETH_FULLDUPLEX_MODE;
          speed = ETH_SPEED_10M;
          break;
        case LAN8720_STATUS_10MBITS_HALFDUPLEX:
          duplex = ETH_HALFDUPLEX_MODE;
          speed = ETH_SPEED_10M;
          break;
        default:
          duplex = ETH_FULLDUPLEX_MODE;
          speed = ETH_SPEED_100M;
          break;
      }

      HAL_ETH_GetMACConfig(&heth, &MACConf);
      MACConf.DuplexMode = duplex;
      MACConf.Speed = speed;
      HAL_ETH_SetMACConfig(&heth, &MACConf);

      HAL_ETH_Start_IT(&heth);
      __HAL_ETH_DMA_DISABLE_IT(&heth, ETH_DMAIER_FBEIE | ETH_DMAIER_AISE | ETH_DMAIER_RBUIE);

      netif_set_up(netif);
      netif_set_link_up(netif);
      link_up_prev = 1;
    }
  }
  else
  {
    Error_Handler();
  }
#endif /* LWIP_ARP || LWIP_ETHERNET */
}

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
  ETH_BufferTypeDef Txbuffer[ETH_TX_DESC_CNT] = {0};
  struct pbuf *q;
  uint32_t i = 0U;
  err_t errval = ERR_OK;

  for (q = p; q != NULL; q = q->next)
  {
    if (i >= ETH_TX_DESC_CNT)
    {
      return ERR_IF;
    }
    Txbuffer[i].buffer = q->payload;
    Txbuffer[i].len = q->len;
    if (i > 0U)
    {
      Txbuffer[i - 1].next = &Txbuffer[i];
    }
    i++;
  }

  TxConfig.Length = p->tot_len;
  TxConfig.TxBuffer = Txbuffer;
  TxConfig.pData = NULL;   /* blocking transmit: lwIP core frees the pbuf */

  if (HAL_ETH_Transmit(&heth, &TxConfig, ETH_DMA_TRANSMIT_TIMEOUT) != HAL_OK)
  {
    errval = ERR_IF;
  }

  /* Release the Tx descriptor slot (calls TxFreeCallback(pData)=NULL -> no-op) */
  HAL_ETH_ReleaseTxPacket(&heth);

  return errval;
}

static struct pbuf *low_level_input(struct netif *netif)
{
  struct pbuf *p = NULL;
  if (RxAllocStatus == RX_ALLOC_OK)
  {
    HAL_ETH_ReadData(&heth, (void **)&p);
  }
  return p;
}

/*******************************************************************************
                       Link monitoring (FreeRTOS task)
*******************************************************************************/
void ethernet_link_status_updated(struct netif *netif)
{
  (void)netif;
  /* ethernet_link_check() already drives netif_set_up/down + MAC config */
}

void ethernet_link_check(struct netif *netif)
{
  static uint32_t last_check = 0U;
  uint32_t now;
  int32_t PHYLinkState;
  uint32_t duplex = ETH_FULLDUPLEX_MODE, speed = ETH_SPEED_100M;
  ETH_MACConfigTypeDef MACConf = {0};

  now = HAL_GetTick();
  if ((now - last_check) < 500U)
  {
    return;
  }
  last_check = now;

  PHYLinkState = LAN8720_GetLinkState(&LAN8720);
  uint8_t link_up = (PHYLinkState > LAN8720_STATUS_LINK_DOWN) ? 1U : 0U;

  if (link_up == link_up_prev)
  {
    return;
  }

  if (link_up)
  {
    switch (PHYLinkState)
    {
      case LAN8720_STATUS_100MBITS_FULLDUPLEX:
        duplex = ETH_FULLDUPLEX_MODE; speed = ETH_SPEED_100M; break;
      case LAN8720_STATUS_100MBITS_HALFDUPLEX:
        duplex = ETH_HALFDUPLEX_MODE; speed = ETH_SPEED_100M; break;
      case LAN8720_STATUS_10MBITS_FULLDUPLEX:
        duplex = ETH_FULLDUPLEX_MODE; speed = ETH_SPEED_10M; break;
      case LAN8720_STATUS_10MBITS_HALFDUPLEX:
        duplex = ETH_HALFDUPLEX_MODE; speed = ETH_SPEED_10M; break;
      default:
        duplex = ETH_FULLDUPLEX_MODE; speed = ETH_SPEED_100M; break;
    }

    HAL_ETH_GetMACConfig(&heth, &MACConf);
    MACConf.DuplexMode = duplex;
    MACConf.Speed = speed;
    HAL_ETH_SetMACConfig(&heth, &MACConf);

    HAL_ETH_Start_IT(&heth);
    __HAL_ETH_DMA_DISABLE_IT(&heth, ETH_DMAIER_FBEIE | ETH_DMAIER_AISE | ETH_DMAIER_RBUIE);

    netif_set_up(netif);
    netif_set_link_up(netif);
    link_up_prev = 1;
    printf("ETH: link up (%luM %s)\r\n",
           (unsigned long)(speed == ETH_SPEED_100M ? 100U : 10U),
           (duplex == ETH_FULLDUPLEX_MODE) ? "full-duplex" : "half-duplex");
  }
  else
  {
    HAL_ETH_Stop_IT(&heth);
    netif_set_link_down(netif);
    netif_set_down(netif);
    link_up_prev = 0;
    printf("ETH: link down\r\n");
  }
}

err_t ethernetif_init(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  netif->hostname = "stm32f429";
#endif

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;

#if LWIP_IPV4
#if LWIP_ARP || LWIP_ETHERNET
#if LWIP_ARP
  netif->output = etharp_output;
#endif
#endif
#endif

#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif

  netif->linkoutput = low_level_output;

  /* initialize the hardware */
  low_level_init(netif);

  return ERR_OK;
}

/*******************************************************************************
                       Callback implementations
*******************************************************************************/
void pbuf_free_custom(struct pbuf *p)
{
  struct pbuf_custom *custom_pbuf = (struct pbuf_custom *)p;
  LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);

  if (RxAllocStatus == RX_ALLOC_ERROR)
  {
    RxAllocStatus = RX_ALLOC_OK;
  }
}

void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *handlerEth)
{
  (void)handlerEth;
  struct pbuf *p = NULL;

  if (RxAllocStatus == RX_ALLOC_OK)
  {
    HAL_ETH_ReadData(&heth, (void **)&p);
  }
  if (p != NULL)
  {
    /* Hand the frame to the tcpip_thread (ISR-safe via sys_arch.c) */
    if (tcpip_input(p, &gnetif) != ERR_OK)
    {
      pbuf_free(p);
    }
  }
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
  struct pbuf_custom *p = LWIP_MEMPOOL_ALLOC(RX_POOL);
  if (p != NULL)
  {
    *buff = (uint8_t *)p + offsetof(RxBuff_t, buff);
    p->custom_free_function = pbuf_free_custom;
    pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, p, *buff, ETH_RX_BUF_SIZE);
  }
  else
  {
    RxAllocStatus = RX_ALLOC_ERROR;
    *buff = NULL;
  }
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
  struct pbuf **ppStart = (struct pbuf **)pStart;
  struct pbuf **ppEnd = (struct pbuf **)pEnd;
  struct pbuf *p = NULL;

  p = (struct pbuf *)(buff - offsetof(RxBuff_t, buff));
  p->next = NULL;
  p->tot_len = 0;
  p->len = Length;

  if (!*ppStart)
  {
    *ppStart = p;
  }
  else
  {
    (*ppEnd)->next = p;
  }
  *ppEnd = p;

  for (p = *ppStart; p != NULL; p = p->next)
  {
    p->tot_len += Length;
  }
}

void HAL_ETH_TxFreeCallback(uint32_t *buff)
{
  if (buff != NULL)
  {
    pbuf_free((struct pbuf *)buff);
  }
}

/*******************************************************************************
                       MAC / DMA configuration
*******************************************************************************/
static void mac_dma_init(void)
{
  ETH_MACConfigTypeDef macconf = {0};
  ETH_DMAConfigTypeDef dmaconf = {0};

  macconf.Watchdog = DISABLE;
  macconf.Jabber = DISABLE;
  macconf.InterPacketGapVal = ETH_INTERFRAMEGAP_96BIT;
  macconf.CarrierSenseDuringTransmit = DISABLE;
  macconf.ReceiveOwn = ENABLE;
  macconf.LoopbackMode = DISABLE;
  macconf.ChecksumOffload = ENABLE;            /* HW checksum for IPv4 */
  macconf.RetryTransmission = DISABLE;
  macconf.AutomaticPadCRCStrip = DISABLE;
  macconf.BackOffLimit = ETH_BACKOFFLIMIT_10;
  macconf.DeferralCheck = DISABLE;
  macconf.PauseTime = 0x0U;
  macconf.ZeroQuantaPause = DISABLE;
  macconf.PauseLowThreshold = ETH_PAUSELOWTHRESHOLD_MINUS4;
  macconf.ReceiveFlowControl = DISABLE;
  macconf.TransmitFlowControl = DISABLE;
  macconf.Speed = ETH_SPEED_100M;
  macconf.DuplexMode = ETH_FULLDUPLEX_MODE;
  macconf.UnicastPausePacketDetect = DISABLE;
  macconf.ForwardRxErrorPacket = DISABLE;
  macconf.ForwardRxUndersizedGoodPacket = DISABLE;
  macconf.DropTCPIPChecksumErrorPacket = ENABLE;
  HAL_ETH_SetMACConfig(&heth, &macconf);

  dmaconf.DropTCPIPChecksumErrorFrame = ENABLE;
  dmaconf.ReceiveStoreForward = ENABLE;
  dmaconf.FlushRxPacket = ENABLE;
  dmaconf.TransmitStoreForward = ENABLE;
  dmaconf.TransmitThresholdControl = ETH_TRANSMITTHRESHOLDCONTROL_128BYTES;
  dmaconf.ForwardErrorFrames = DISABLE;
  dmaconf.ForwardUndersizedGoodFrames = DISABLE;
  dmaconf.ReceiveThresholdControl = ETH_RECEIVEDTHRESHOLDCONTROL_32BYTES;
  dmaconf.SecondFrameOperate = DISABLE;
  dmaconf.AddressAlignedBeats = ENABLE;
  dmaconf.BurstMode = ETH_BURSTLENGTH_FIXED;
  dmaconf.RxDMABurstLength = ETH_RXDMABURSTLENGTH_32BEAT;
  dmaconf.TxDMABurstLength = ETH_TXDMABURSTLENGTH_32BEAT;
  dmaconf.EnhancedDescriptorFormat = ENABLE;
  dmaconf.DescriptorSkipLength = 0x0U;
  dmaconf.DMAArbitration = ETH_DMAARBITRATION_ROUNDROBIN_RXTX_1_1;
  HAL_ETH_SetDMAConfig(&heth, &dmaconf);
}

/*******************************************************************************
                       PHY IO Functions
*******************************************************************************/
static int32_t ETH_PHY_IO_Init(void)
{
  HAL_ETH_SetMDIOClockRange(&heth);
  return 0;
}
static int32_t ETH_PHY_IO_DeInit(void)
{
  return 0;
}
static int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal)
{
  if (HAL_ETH_ReadPHYRegister(&heth, DevAddr, RegAddr, pRegVal) != HAL_OK)
  {
    return -1;
  }
  return 0;
}
static int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal)
{
  if (HAL_ETH_WritePHYRegister(&heth, DevAddr, RegAddr, RegVal) != HAL_OK)
  {
    return -1;
  }
  return 0;
}
static int32_t ETH_PHY_IO_GetTick(void)
{
  return (int32_t)HAL_GetTick();
}

/*******************************************************************************
                       ETH MSP
*******************************************************************************/
void HAL_ETH_MspInit(ETH_HandleTypeDef *ethHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if (ethHandle->Instance == ETH)
  {
    __HAL_RCC_ETH_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* ETH GPIO Configuration
       PC1  -> ETH_MDC      PC4 -> ETH_RXD0    PC5 -> ETH_RXD1
       PA1  -> ETH_REF_CLK  PA2 -> ETH_MDIO    PA7 -> ETH_CRS_DV
       PB11 -> ETH_TX_EN    PG13 -> ETH_TXD0   PG14 -> ETH_TXD1 */
    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_13 | GPIO_PIN_14;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

    /* Peripheral interrupt init */
    HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(ETH_IRQn);
  }
}

void HAL_ETH_MspDeInit(ETH_HandleTypeDef *ethHandle)
{
  if (ethHandle->Instance == ETH)
  {
    __HAL_RCC_ETH_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7);
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_11);
    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_13 | GPIO_PIN_14);

    HAL_NVIC_DisableIRQ(ETH_IRQn);
  }
}

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */
