/**
  ******************************************************************************
  * @file    lan8720.c
  * @brief   LAN8720A PHY driver implementation.
  ******************************************************************************
  */
#include "lan8720.h"
#include <stdint.h>

/* IO context registered from ethernetif.c */
static lan8720_IOCtx_t LAN8720_IOCtx;

int32_t LAN8720_RegisterBusIO(lan8720_Object_t *pObj, const lan8720_IOCtx_t *pIOCtx)
{
  if (pObj == NULL || pIOCtx == NULL ||
      pIOCtx->ReadReg == NULL || pIOCtx->WriteReg == NULL ||
      pIOCtx->Init == NULL)
  {
    return LAN8720_STATUS_ERROR;
  }

  pObj->DevAddr = LAN8720_ADDR;
  LAN8720_IOCtx = *pIOCtx;
  return LAN8720_STATUS_OK;
}

int32_t LAN8720_Init(lan8720_Object_t *pObj)
{
  uint32_t regval = 0U;
  uint32_t tickstart = 0U;

  if (LAN8720_IOCtx.Init() < 0)
  {
    return LAN8720_STATUS_ERROR;
  }

  /* Software reset the PHY */
  if (LAN8720_IOCtx.WriteReg(pObj->DevAddr, LAN8720_BCR, LAN8720_BCR_SOFT_RESET) < 0)
  {
    return LAN8720_STATUS_ERROR;
  }

  /* Wait for the reset to complete (bit self-clears) */
  tickstart = LAN8720_IOCtx.GetTick();
  do
  {
    if (LAN8720_IOCtx.ReadReg(pObj->DevAddr, LAN8720_BCR, &regval) < 0)
    {
      return LAN8720_STATUS_ERROR;
    }
  } while ((regval & LAN8720_BCR_SOFT_RESET) == LAN8720_BCR_SOFT_RESET &&
           ((LAN8720_IOCtx.GetTick() - tickstart) < 500U));

  /* Enable auto-negotiation and restart it */
  regval = (uint32_t)(LAN8720_BCR_AUTONEG_EN | LAN8720_BCR_RESTART_AUTONEG);
  if (LAN8720_IOCtx.WriteReg(pObj->DevAddr, LAN8720_BCR, regval) < 0)
  {
    return LAN8720_STATUS_ERROR;
  }

  return LAN8720_STATUS_OK;
}

int32_t LAN8720_GetLinkState(lan8720_Object_t *pObj)
{
  uint32_t bsr = 0U;
  uint32_t scsr = 0U;

  if (LAN8720_IOCtx.ReadReg(pObj->DevAddr, LAN8720_BSR, &bsr) < 0)
  {
    return LAN8720_STATUS_LINK_DOWN;
  }

  /* Link status bit (2) must be set */
  if ((bsr & LAN8720_BSR_LINK_STATUS) == 0U)
  {
    return LAN8720_STATUS_LINK_DOWN;
  }

  /* Read the special control/status register to get the resolved speed/duplex */
  if (LAN8720_IOCtx.ReadReg(pObj->DevAddr, LAN8720_SCSR, &scsr) < 0)
  {
    /* fall back to 100M full-duplex (most common link) */
    return LAN8720_STATUS_100MBITS_FULLDUPLEX;
  }

  /* Only trust the speed field once auto-negotiation is done */
  if ((bsr & LAN8720_BSR_AUTONEG_COMPLETE) != 0U)
  {
    switch (scsr & LAN8720_SCSR_SPEED_MASK)
    {
      case 0x0000U:  /* 10BASE-T half-duplex */
        return LAN8720_STATUS_10MBITS_HALFDUPLEX;
      case 0x0004U:  /* 10BASE-T full-duplex */
        return LAN8720_STATUS_10MBITS_FULLDUPLEX;
      case 0x0008U:  /* 100BASE-TX half-duplex */
        return LAN8720_STATUS_100MBITS_HALFDUPLEX;
      case 0x000CU:  /* 100BASE-TX full-duplex */
        return LAN8720_STATUS_100MBITS_FULLDUPLEX;
      default:
        break;
    }
  }

  /* Safe default until auto-negotiation resolves */
  return LAN8720_STATUS_100MBITS_FULLDUPLEX;
}

int32_t LAN8720_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal)
{
  return LAN8720_IOCtx.ReadReg(DevAddr, RegAddr, pRegVal);
}

int32_t LAN8720_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal)
{
  return LAN8720_IOCtx.WriteReg(DevAddr, RegAddr, RegVal);
}

int32_t LAN8720_GetTick(void)
{
  return LAN8720_IOCtx.GetTick();
}
