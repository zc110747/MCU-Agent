/**
  ******************************************************************************
  * @file    lan8720.h
  * @brief   LAN8720A PHY driver (RMII, address 0) for STM32F429 ETH.
  ******************************************************************************
  */
#ifndef LAN8720_H
#define LAN8720_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

/* LAN8720 PHY address (strapped to 0 on the board) */
#define LAN8720_ADDR       0U

/* PHY register map (standard IEEE + LAN8720 specific) */
#define LAN8720_BCR        0U    /* Basic Control Register          */
#define LAN8720_BSR        1U    /* Basic Status Register           */
#define LAN8720_PHYID1     2U    /* PHY Identifier 1                */
#define LAN8720_PHYID2     3U    /* PHY Identifier 2                */
#define LAN8720_SCSR       31U   /* Special Control/Status Register */

/* Basic Control Register bits */
#define LAN8720_BCR_SOFT_RESET        ((uint16_t)0x8000U)
#define LAN8720_BCR_AUTONEG_EN        ((uint16_t)0x1000U)
#define LAN8720_BCR_RESTART_AUTONEG   ((uint16_t)0x0200U)

/* Basic Status Register bits */
#define LAN8720_BSR_LINK_STATUS        ((uint16_t)0x0004U)  /* bit 2  */
#define LAN8720_BSR_AUTONEG_ABILITY   ((uint16_t)0x0008U)  /* bit 3  */
#define LAN8720_BSR_AUTONEG_COMPLETE  ((uint16_t)0x0020U)  /* bit 5  */

/* Special Control/Status Register (0x1F) bits */
/* bits [3:2] => Speed Indication:
     00 = 10BASE-T HD, 01 = 10BASE-T FD, 10 = 100BASE-TX HD, 11 = 100BASE-TX FD */
#define LAN8720_SCSR_SPEED_MASK        ((uint16_t)0x000CU)
#define LAN8720_SCSR_AUTONEG_DONE     ((uint16_t)0x0002U)  /* bit 1  */

/* Return / status codes */
#define LAN8720_STATUS_OK    0
#define LAN8720_STATUS_ERROR -1

/* Link state enumeration (matches the ethernetif link switch) */
typedef enum
{
  LAN8720_STATUS_LINK_DOWN          = 0,
  LAN8720_STATUS_10MBITS_HALFDUPLEX,
  LAN8720_STATUS_10MBITS_FULLDUPLEX,
  LAN8720_STATUS_100MBITS_HALFDUPLEX,
  LAN8720_STATUS_100MBITS_FULLDUPLEX
} LAN8720_StatusTypeDef;

/* PHY object */
typedef struct
{
  uint32_t DevAddr;
} lan8720_Object_t;

/* IO context function pointers (filled by ethernetif) */
typedef struct
{
  int32_t (*Init)(void);
  int32_t (*DeInit)(void);
  int32_t (*ReadReg)(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal);
  int32_t (*WriteReg)(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal);
  int32_t (*GetTick)(void);
} lan8720_IOCtx_t;

int32_t LAN8720_RegisterBusIO(lan8720_Object_t *pObj, const lan8720_IOCtx_t *pIOCtx);
int32_t LAN8720_Init(lan8720_Object_t *pObj);
int32_t LAN8720_GetLinkState(lan8720_Object_t *pObj);
int32_t LAN8720_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal);
int32_t LAN8720_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal);
int32_t LAN8720_GetTick(void);

#ifdef __cplusplus
}
#endif

#endif /* LAN8720_H */
