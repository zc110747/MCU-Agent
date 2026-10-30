/**
  ******************************************************************************
  * @file      startup_stm32f429xx.s
  * @brief     STM32F429xx Devices vector table for GNU ARM toolchain.
  *            This module performs:
  *            - Set the initial SP (from _estack)
  *            - Set the initial PC == Reset_Handler
  *            - Set the vector table entries with the exceptions ISR address
  *            - Copy .data from FLASH to RAM, zero .bss
  *            - Branch to main (via SystemInit + main)
  ******************************************************************************
  */
  .syntax unified
  .cpu cortex-m4
  .fpu softvfp
  .thumb

.global g_pfnVectors
.global Default_Handler

/* start address for the initialization values of the .data section */
.word _sidata
/* start address for the .data section */
.word _sdata
/* end address for the .data section */
.word _edata
/* start address for the .bss section */
.word _sbss
/* end address for the .bss section */
.word _ebss

  .section .text.Reset_Handler
  .weak Reset_Handler
  .type Reset_Handler, %function
Reset_Handler:
  ldr   sp, =_estack      /* set stack pointer */

/* Copy the .data segment initializers from flash to SRAM */
  ldr   r0, =_sdata
  ldr   r1, =_edata
  ldr   r2, =_sidata
  movs  r3, #0
  b  LoopCopyDataInit
CopyDataInit:
  ldr  r4, [r2, r3]
  str  r4, [r0, r3]
  adds r3, r3, #4
LoopCopyDataInit:
  adds r4, r0, r3
  cmp  r4, r1
  bcc  CopyDataInit

/* Zero fill the .bss section */
  ldr  r2, =_sbss
  ldr  r4, =_ebss
  movs r3, #0
  b  LoopFillZerobss
FillZerobss:
  str  r3, [r2]
  adds r2, r2, #4
LoopFillZerobss:
  cmp  r2, r4
  bcc  FillZerobss

/* Call the clock system initialization function */
  bl  SystemInit
/* Call static constructors */
  bl  __libc_init_array
/* Call the application's entry point */
  bl  main
  bx  lr
.size Reset_Handler, .-Reset_Handler

/**
 * @brief  This is the code that gets called when the processor receives an
 *         unexpected interrupt.  This simply enters an infinite loop.
 */
  .section .text.Default_Handler,"ax",%progbits
Default_Handler:
Infinite_Loop:
  b  Infinite_Loop
  .size Default_Handler, .-Default_Handler

/******************************************************************************
* Cortex-M4 core handlers
******************************************************************************/
  .weak NMI_Handler
  .thumb_set NMI_Handler,Default_Handler

  .weak HardFault_Handler
  .thumb_set HardFault_Handler,Default_Handler

  .weak MemManage_Handler
  .thumb_set MemManage_Handler,Default_Handler

  .weak BusFault_Handler
  .thumb_set BusFault_Handler,Default_Handler

  .weak UsageFault_Handler
  .thumb_set UsageFault_Handler,Default_Handler

  .weak SVC_Handler
  .thumb_set SVC_Handler,Default_Handler

  .weak DebugMon_Handler
  .thumb_set DebugMon_Handler,Default_Handler

  .weak PendSV_Handler
  .thumb_set PendSV_Handler,Default_Handler

  .weak SysTick_Handler
  .thumb_set SysTick_Handler,Default_Handler

/******************************************************************************
* STM32F429xx specific handlers
******************************************************************************/
  .weak WWDG_IRQHandler
  .thumb_set WWDG_IRQHandler,Default_Handler

  .weak PVD_IRQHandler
  .thumb_set PVD_IRQHandler,Default_Handler

  .weak TAMP_STAMP_IRQHandler
  .thumb_set TAMP_STAMP_IRQHandler,Default_Handler

  .weak RTC_WKUP_IRQHandler
  .thumb_set RTC_WKUP_IRQHandler,Default_Handler

  .weak FLASH_IRQHandler
  .thumb_set FLASH_IRQHandler,Default_Handler

  .weak RCC_IRQHandler
  .thumb_set RCC_IRQHandler,Default_Handler

  .weak EXTI0_IRQHandler
  .thumb_set EXTI0_IRQHandler,Default_Handler

  .weak EXTI1_IRQHandler
  .thumb_set EXTI1_IRQHandler,Default_Handler

  .weak EXTI2_IRQHandler
  .thumb_set EXTI2_IRQHandler,Default_Handler

  .weak EXTI3_IRQHandler
  .thumb_set EXTI3_IRQHandler,Default_Handler

  .weak EXTI4_IRQHandler
  .thumb_set EXTI4_IRQHandler,Default_Handler

  .weak DMA1_Stream0_IRQHandler
  .thumb_set DMA1_Stream0_IRQHandler,Default_Handler

  .weak DMA1_Stream1_IRQHandler
  .thumb_set DMA1_Stream1_IRQHandler,Default_Handler

  .weak DMA1_Stream2_IRQHandler
  .thumb_set DMA1_Stream2_IRQHandler,Default_Handler

  .weak DMA1_Stream3_IRQHandler
  .thumb_set DMA1_Stream3_IRQHandler,Default_Handler

  .weak DMA1_Stream4_IRQHandler
  .thumb_set DMA1_Stream4_IRQHandler,Default_Handler

  .weak DMA1_Stream5_IRQHandler
  .thumb_set DMA1_Stream5_IRQHandler,Default_Handler

  .weak DMA1_Stream6_IRQHandler
  .thumb_set DMA1_Stream6_IRQHandler,Default_Handler

  .weak ADC_IRQHandler
  .thumb_set ADC_IRQHandler,Default_Handler

  .weak CAN1_TX_IRQHandler
  .thumb_set CAN1_TX_IRQHandler,Default_Handler

  .weak CAN1_RX0_IRQHandler
  .thumb_set CAN1_RX0_IRQHandler,Default_Handler

  .weak CAN1_RX1_IRQHandler
  .thumb_set CAN1_RX1_IRQHandler,Default_Handler

  .weak CAN1_SCE_IRQHandler
  .thumb_set CAN1_SCE_IRQHandler,Default_Handler

  .weak EXTI9_5_IRQHandler
  .thumb_set EXTI9_5_IRQHandler,Default_Handler

  .weak TIM1_BRK_TIM9_IRQHandler
  .thumb_set TIM1_BRK_TIM9_IRQHandler,Default_Handler

  .weak TIM1_UP_TIM10_IRQHandler
  .thumb_set TIM1_UP_TIM10_IRQHandler,Default_Handler

  .weak TIM1_TRG_COM_TIM11_IRQHandler
  .thumb_set TIM1_TRG_COM_TIM11_IRQHandler,Default_Handler

  .weak TIM1_CC_IRQHandler
  .thumb_set TIM1_CC_IRQHandler,Default_Handler

  .weak TIM2_IRQHandler
  .thumb_set TIM2_IRQHandler,Default_Handler

  .weak TIM3_IRQHandler
  .thumb_set TIM3_IRQHandler,Default_Handler

  .weak TIM4_IRQHandler
  .thumb_set TIM4_IRQHandler,Default_Handler

  .weak I2C1_EV_IRQHandler
  .thumb_set I2C1_EV_IRQHandler,Default_Handler

  .weak I2C1_ER_IRQHandler
  .thumb_set I2C1_ER_IRQHandler,Default_Handler

  .weak I2C2_EV_IRQHandler
  .thumb_set I2C2_EV_IRQHandler,Default_Handler

  .weak I2C2_ER_IRQHandler
  .thumb_set I2C2_ER_IRQHandler,Default_Handler

  .weak SPI1_IRQHandler
  .thumb_set SPI1_IRQHandler,Default_Handler

  .weak SPI2_IRQHandler
  .thumb_set SPI2_IRQHandler,Default_Handler

  .weak USART1_IRQHandler
  .thumb_set USART1_IRQHandler,Default_Handler

  .weak USART2_IRQHandler
  .thumb_set USART2_IRQHandler,Default_Handler

  .weak USART3_IRQHandler
  .thumb_set USART3_IRQHandler,Default_Handler

  .weak EXTI15_10_IRQHandler
  .thumb_set EXTI15_10_IRQHandler,Default_Handler

  .weak RTC_Alarm_IRQHandler
  .thumb_set RTC_Alarm_IRQHandler,Default_Handler

  .weak OTG_FS_WKUP_IRQHandler
  .thumb_set OTG_FS_WKUP_IRQHandler,Default_Handler

  .weak TIM8_BRK_TIM12_IRQHandler
  .thumb_set TIM8_BRK_TIM12_IRQHandler,Default_Handler

  .weak TIM8_UP_TIM13_IRQHandler
  .thumb_set TIM8_UP_TIM13_IRQHandler,Default_Handler

  .weak TIM8_TRG_COM_TIM14_IRQHandler
  .thumb_set TIM8_TRG_COM_TIM14_IRQHandler,Default_Handler

  .weak TIM8_CC_IRQHandler
  .thumb_set TIM8_CC_IRQHandler,Default_Handler

  .weak DMA1_Stream7_IRQHandler
  .thumb_set DMA1_Stream7_IRQHandler,Default_Handler

  .weak FMC_IRQHandler
  .thumb_set FMC_IRQHandler,Default_Handler

  .weak SDIO_IRQHandler
  .thumb_set SDIO_IRQHandler,Default_Handler

  .weak TIM5_IRQHandler
  .thumb_set TIM5_IRQHandler,Default_Handler

  .weak SPI3_IRQHandler
  .thumb_set SPI3_IRQHandler,Default_Handler

  .weak UART4_IRQHandler
  .thumb_set UART4_IRQHandler,Default_Handler

  .weak UART5_IRQHandler
  .thumb_set UART5_IRQHandler,Default_Handler

  .weak TIM6_DAC_IRQHandler
  .thumb_set TIM6_DAC_IRQHandler,Default_Handler

  .weak TIM7_IRQHandler
  .thumb_set TIM7_IRQHandler,Default_Handler

  .weak DMA2_Stream0_IRQHandler
  .thumb_set DMA2_Stream0_IRQHandler,Default_Handler

  .weak DMA2_Stream1_IRQHandler
  .thumb_set DMA2_Stream1_IRQHandler,Default_Handler

  .weak DMA2_Stream2_IRQHandler
  .thumb_set DMA2_Stream2_IRQHandler,Default_Handler

  .weak DMA2_Stream3_IRQHandler
  .thumb_set DMA2_Stream3_IRQHandler,Default_Handler

  .weak DMA2_Stream4_IRQHandler
  .thumb_set DMA2_Stream4_IRQHandler,Default_Handler

  .weak ETH_IRQHandler
  .thumb_set ETH_IRQHandler,Default_Handler

  .weak ETH_WKUP_IRQHandler
  .thumb_set ETH_WKUP_IRQHandler,Default_Handler

  .weak CAN2_TX_IRQHandler
  .thumb_set CAN2_TX_IRQHandler,Default_Handler

  .weak CAN2_RX0_IRQHandler
  .thumb_set CAN2_RX0_IRQHandler,Default_Handler

  .weak CAN2_RX1_IRQHandler
  .thumb_set CAN2_RX1_IRQHandler,Default_Handler

  .weak CAN2_SCE_IRQHandler
  .thumb_set CAN2_SCE_IRQHandler,Default_Handler

  .weak OTG_FS_IRQHandler
  .thumb_set OTG_FS_IRQHandler,Default_Handler

  .weak DMA2_Stream5_IRQHandler
  .thumb_set DMA2_Stream5_IRQHandler,Default_Handler

  .weak DMA2_Stream6_IRQHandler
  .thumb_set DMA2_Stream6_IRQHandler,Default_Handler

  .weak DMA2_Stream7_IRQHandler
  .thumb_set DMA2_Stream7_IRQHandler,Default_Handler

  .weak USART6_IRQHandler
  .thumb_set USART6_IRQHandler,Default_Handler

  .weak I2C3_EV_IRQHandler
  .thumb_set I2C3_EV_IRQHandler,Default_Handler

  .weak I2C3_ER_IRQHandler
  .thumb_set I2C3_ER_IRQHandler,Default_Handler

  .weak OTG_HS_EP1_OUT_IRQHandler
  .thumb_set OTG_HS_EP1_OUT_IRQHandler,Default_Handler

  .weak OTG_HS_EP1_IN_IRQHandler
  .thumb_set OTG_HS_EP1_IN_IRQHandler,Default_Handler

  .weak OTG_HS_WKUP_IRQHandler
  .thumb_set OTG_HS_WKUP_IRQHandler,Default_Handler

  .weak OTG_HS_IRQHandler
  .thumb_set OTG_HS_IRQHandler,Default_Handler

  .weak DCMI_IRQHandler
  .thumb_set DCMI_IRQHandler,Default_Handler

  .weak CRYP_IRQHandler
  .thumb_set CRYP_IRQHandler,Default_Handler

  .weak HASH_RNG_IRQHandler
  .thumb_set HASH_RNG_IRQHandler,Default_Handler

  .weak FPU_IRQHandler
  .thumb_set FPU_IRQHandler,Default_Handler

  .weak UART7_IRQHandler
  .thumb_set UART7_IRQHandler,Default_Handler

  .weak UART8_IRQHandler
  .thumb_set UART8_IRQHandler,Default_Handler

  .weak SPI4_IRQHandler
  .thumb_set SPI4_IRQHandler,Default_Handler

  .weak SPI5_IRQHandler
  .thumb_set SPI5_IRQHandler,Default_Handler

  .weak SPI6_IRQHandler
  .thumb_set SPI6_IRQHandler,Default_Handler

  .weak SAI1_IRQHandler
  .thumb_set SAI1_IRQHandler,Default_Handler

  .weak LTDC_IRQHandler
  .thumb_set LTDC_IRQHandler,Default_Handler

  .weak LTDC_ER_IRQHandler
  .thumb_set LTDC_ER_IRQHandler,Default_Handler

  .weak DMA2D_IRQHandler
  .thumb_set DMA2D_IRQHandler,Default_Handler

/******************************************************************************
* The vector table
******************************************************************************/
  .section .isr_vector,"a",%progbits
  .type g_pfnVectors, %object
  .size g_pfnVectors, .-g_pfnVectors

g_pfnVectors:
  .word _estack              /* Top of Stack */
  .word Reset_Handler        /* Reset Handler */
  .word NMI_Handler          /* NMI Handler */
  .word HardFault_Handler    /* Hard Fault Handler */
  .word MemManage_Handler    /* MPU Fault Handler */
  .word BusFault_Handler     /* Bus Fault Handler */
  .word UsageFault_Handler   /* Usage Fault Handler */
  .word 0                    /* Reserved */
  .word 0                    /* Reserved */
  .word 0                    /* Reserved */
  .word 0                    /* Reserved */
  .word SVC_Handler      /* SVCall Handler (FreeRTOS) */
  .word DebugMon_Handler     /* Debug Monitor Handler */
  .word 0                    /* Reserved */
  .word PendSV_Handler  /* PendSV Handler (FreeRTOS) */
  .word SysTick_Handler  /* SysTick Handler (FreeRTOS) */
  /* External Interrupts */
  .word WWDG_IRQHandler              /* Window WatchDog */
  .word PVD_IRQHandler               /* PVD through EXTI Line detection */
  .word TAMP_STAMP_IRQHandler        /* Tamper and TimeStamps */
  .word RTC_WKUP_IRQHandler          /* RTC Wakeup */
  .word FLASH_IRQHandler             /* FLASH */
  .word RCC_IRQHandler               /* RCC */
  .word EXTI0_IRQHandler             /* EXTI Line0 */
  .word EXTI1_IRQHandler             /* EXTI Line1 */
  .word EXTI2_IRQHandler             /* EXTI Line2 */
  .word EXTI3_IRQHandler             /* EXTI Line3 */
  .word EXTI4_IRQHandler             /* EXTI Line4 */
  .word DMA1_Stream0_IRQHandler      /* DMA1 Stream 0 */
  .word DMA1_Stream1_IRQHandler      /* DMA1 Stream 1 */
  .word DMA1_Stream2_IRQHandler      /* DMA1 Stream 2 */
  .word DMA1_Stream3_IRQHandler      /* DMA1 Stream 3 */
  .word DMA1_Stream4_IRQHandler      /* DMA1 Stream 4 */
  .word DMA1_Stream5_IRQHandler      /* DMA1 Stream 5 */
  .word DMA1_Stream6_IRQHandler      /* DMA1 Stream 6 */
  .word ADC_IRQHandler               /* ADC1, ADC2, ADC3 */
  .word CAN1_TX_IRQHandler           /* CAN1 TX */
  .word CAN1_RX0_IRQHandler          /* CAN1 RX0 */
  .word CAN1_RX1_IRQHandler          /* CAN1 RX1 */
  .word CAN1_SCE_IRQHandler          /* CAN1 SCE */
  .word EXTI9_5_IRQHandler           /* EXTI Line[9:5] */
  .word TIM1_BRK_TIM9_IRQHandler     /* TIM1 Break, TIM9 */
  .word TIM1_UP_TIM10_IRQHandler     /* TIM1 Update, TIM10 */
  .word TIM1_TRG_COM_TIM11_IRQHandler/* TIM1 Trigger, TIM11 */
  .word TIM1_CC_IRQHandler           /* TIM1 Capture Compare */
  .word TIM2_IRQHandler              /* TIM2 */
  .word TIM3_IRQHandler              /* TIM3 */
  .word TIM4_IRQHandler              /* TIM4 */
  .word I2C1_EV_IRQHandler           /* I2C1 Event */
  .word I2C1_ER_IRQHandler           /* I2C1 Error */
  .word I2C2_EV_IRQHandler           /* I2C2 Event */
  .word I2C2_ER_IRQHandler           /* I2C2 Error */
  .word SPI1_IRQHandler              /* SPI1 */
  .word SPI2_IRQHandler              /* SPI2 */
  .word USART1_IRQHandler            /* USART1 */
  .word USART2_IRQHandler            /* USART2 */
  .word USART3_IRQHandler            /* USART3 */
  .word EXTI15_10_IRQHandler         /* EXTI Line[15:10] */
  .word RTC_Alarm_IRQHandler         /* RTC Alarm */
  .word OTG_FS_WKUP_IRQHandler       /* USB OTG FS Wakeup */
  .word TIM8_BRK_TIM12_IRQHandler    /* TIM8 Break, TIM12 */
  .word TIM8_UP_TIM13_IRQHandler     /* TIM8 Update, TIM13 */
  .word TIM8_TRG_COM_TIM14_IRQHandler/* TIM8 Trigger, TIM14 */
  .word TIM8_CC_IRQHandler           /* TIM8 Capture Compare */
  .word DMA1_Stream7_IRQHandler      /* DMA1 Stream 7 */
  .word FMC_IRQHandler               /* FMC */
  .word SDIO_IRQHandler              /* SDIO */
  .word TIM5_IRQHandler              /* TIM5 */
  .word SPI3_IRQHandler              /* SPI3 */
  .word UART4_IRQHandler             /* UART4 */
  .word UART5_IRQHandler             /* UART5 */
  .word TIM6_DAC_IRQHandler          /* TIM6, DAC */
  .word TIM7_IRQHandler              /* TIM7 */
  .word DMA2_Stream0_IRQHandler      /* DMA2 Stream 0 */
  .word DMA2_Stream1_IRQHandler      /* DMA2 Stream 1 */
  .word DMA2_Stream2_IRQHandler      /* DMA2 Stream 2 */
  .word DMA2_Stream3_IRQHandler      /* DMA2 Stream 3 */
  .word DMA2_Stream4_IRQHandler      /* DMA2 Stream 4 */
  .word ETH_IRQHandler               /* Ethernet */
  .word ETH_WKUP_IRQHandler          /* Ethernet Wakeup */
  .word CAN2_TX_IRQHandler           /* CAN2 TX */
  .word CAN2_RX0_IRQHandler          /* CAN2 RX0 */
  .word CAN2_RX1_IRQHandler          /* CAN2 RX1 */
  .word CAN2_SCE_IRQHandler          /* CAN2 SCE */
  .word OTG_FS_IRQHandler            /* USB OTG FS */
  .word DMA2_Stream5_IRQHandler      /* DMA2 Stream 5 */
  .word DMA2_Stream6_IRQHandler      /* DMA2 Stream 6 */
  .word DMA2_Stream7_IRQHandler      /* DMA2 Stream 7 */
  .word USART6_IRQHandler            /* USART6 */
  .word I2C3_EV_IRQHandler           /* I2C3 Event */
  .word I2C3_ER_IRQHandler           /* I2C3 Error */
  .word OTG_HS_EP1_OUT_IRQHandler    /* USB OTG HS EP1 OUT */
  .word OTG_HS_EP1_IN_IRQHandler     /* USB OTG HS EP1 IN */
  .word OTG_HS_WKUP_IRQHandler       /* USB OTG HS Wakeup */
  .word OTG_HS_IRQHandler            /* USB OTG HS */
  .word DCMI_IRQHandler              /* DCMI */
  .word CRYP_IRQHandler              /* CRYP (reserved on F429) */
  .word HASH_RNG_IRQHandler          /* HASH, RNG */
  .word FPU_IRQHandler               /* FPU */
  .word UART7_IRQHandler             /* UART7 */
  .word UART8_IRQHandler             /* UART8 */
  .word SPI4_IRQHandler              /* SPI4 */
  .word SPI5_IRQHandler              /* SPI5 */
  .word SPI6_IRQHandler              /* SPI6 */
  .word SAI1_IRQHandler              /* SAI1 */
  .word LTDC_IRQHandler              /* LTDC */
  .word LTDC_ER_IRQHandler           /* LTDC Error */
  .word DMA2D_IRQHandler             /* DMA2D */
