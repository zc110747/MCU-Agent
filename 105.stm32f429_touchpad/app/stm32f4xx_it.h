/**
  ******************************************************************************
  * @file    stm32f4xx_it.h
  * @brief   Interrupt handler prototypes.
  ******************************************************************************
  */
#ifndef __STM32F4xx_IT_H
#define __STM32F4xx_IT_H

#ifdef __cplusplus
extern "C" {
#endif

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);
void TIM7_IRQHandler(void);
void EXTI9_5_IRQHandler(void);
void OTG_FS_IRQHandler(void);
void SDIO_IRQHandler(void);
void DMA2_Stream3_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __STM32F4xx_IT_H */
