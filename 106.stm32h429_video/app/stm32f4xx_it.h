/**
 * @file    stm32f4xx_it.h
 * @brief   Interrupt handler prototypes (all other IRQs come from the HAL).
 */
#ifndef STM32F4XX_IT_H
#define STM32F4XX_IT_H

#ifdef __cplusplus
extern "C" {
#endif

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);

void TIM8_BRK_TIM12_IRQHandler(void);
void DMA2_Stream3_IRQHandler(void);
void DMA2_Stream6_IRQHandler(void);
void SDIO_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* STM32F4XX_IT_H */
