/**
  * @file    periph_map.h
  * @brief   Readable aliases for CMSIS peripheral instances
  *
  *          Groups the CubeMX hardware mapping (DMA channels, TIM/SPI/GPIO
  *          instances) in one place so regeneration only needs editing this
  *          file.  Must match stm32f1xx_hal_msp.c.
  */
#ifndef PERIPH_MAP_H
#define PERIPH_MAP_H

#include "stm32f1xx_hal.h"

/* ---- Pipeline: camera -> FIFO -> LCD ---- */

#define PIPELINE_CAM_DMA         DMA1            /* camera DMA controller (DMA1) */
#define PIPELINE_CAM_DMA_CHNL    DMA1_Channel3   /* OV7670 data DMA channel (TIM3 CC4 trigger) */
#define PIPELINE_CAM_DMA_IRQN    DMA1_Channel3_IRQn
#define PIPELINE_CAM_TIM         TIM3            /* camera RCK PWM */
#define PIPELINE_LCD_DMA         DMA1            /* LCD DMA controller (DMA1) */
#define PIPELINE_LCD_DMA_CHNL    DMA1_Channel5   /* LCD SPI2 TX DMA channel */
#define PIPELINE_LCD_DMA_IRQN    DMA1_Channel5_IRQn
#define PIPELINE_LCD_SPI         SPI2            /* LCD SPI */
#define PIPELINE_EXTI            EXTI            /* VSYNC EXTI controller */

/* DMA flags, bound to the channel aliases above */
#define PIPELINE_CAM_DMA_CLEAR     DMA_IFCR_CGIF3  /* clear all cam channel flags */
#define PIPELINE_CAM_DMA_HTIF      DMA_ISR_HTIF3
#define PIPELINE_CAM_DMA_TCIF      DMA_ISR_TCIF3
#define PIPELINE_CAM_DMA_CLR_HTIF  DMA_IFCR_CHTIF3
#define PIPELINE_CAM_DMA_CLR_TCIF  DMA_IFCR_CTCIF3
#define PIPELINE_LCD_DMA_CLEAR     DMA_IFCR_CGIF5

/* RCK = PB1, CRL bits [7:4]. */
#define PIPELINE_RCK_CRL_MASK       (0x0Fu << 4u)
#define PIPELINE_RCK_CRL_OUTPUT_PP  (0x03u << 4u)  /* CNF=00, MODE=11 */
#define PIPELINE_RCK_CRL_AF_PP      (0x0Bu << 4u)  /* CNF=10, MODE=11 */

#endif /* PERIPH_MAP_H */
