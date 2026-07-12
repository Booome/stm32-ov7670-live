/**
  * @file    pipeline.h
  * @brief   Dual-DMA frame capture pipeline (Camera -> FIFO -> LCD)
  *
  *          Camera DMA (Circular) reads GPIOA->IDR into PipelineBuffer[640],
  *          SPI DMA (Normal) sends 320B half-buffers to ST7735 LCD.
  *
  *          Frame: 160x128 RGB565 = 40960 bytes = 128 x 320B half-buffers
  *          VSYNC triggered, DWT non-blocking 1.3ms delay before read start.
  *
  *          Requires: DWT_Init, SCCB_Init, OV7670_Init, LCD_Init,
  *          MX_TIM3_Init, MX_SPI2_Init, MX_DMA_Init before Pipeline_Init.
  */
#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdint.h>
#include <stdbool.h>
#include "dwt_delay.h"
#include "main.h"
#include "stm32f1xx_hal.h"

/* TIM3 handle (defined in main.c) */
extern TIM_HandleTypeDef htim3;

/* ---- Pipeline init helpers (static inline) ---- */

/** @brief  Enable TIM3 CC4 DMA request (triggers Camera DMA per RCK cycle) */
static inline void Pipeline_EnableTimDma(void)
{
  __HAL_TIM_ENABLE_DMA(&htim3, TIM_DMA_CC4);
}

/** @brief  Clear pending VSYNC EXTI interrupt flag */
static inline void Pipeline_ClearVsyncPending(void)
{
  __HAL_GPIO_EXTI_CLEAR_IT(OV7670_VSYNC_Pin);
}

/** @brief  Enable EXTI15_10 NVIC IRQ (VSYNC on PA11 -> EXTI11) */
static inline void Pipeline_EnableVsyncIrq(void)
{
  HAL_NVIC_EnableIRQ(OV7670_VSYNC_EXTI_IRQn);
}

/* ---- Public API ---- */

/** @brief Pipeline states */
typedef enum
{
  PIPELINE_STATE_DISABLED = 0,    /**< Not yet initialized            */
  PIPELINE_STATE_IDLE,            /**< Waiting for VSYNC              */
  PIPELINE_STATE_FRAME_START,     /**< VSYNC received, waiting 1.3ms  */
  PIPELINE_STATE_FRAME_CAPTURING, /**< DMA pipeline active            */
  PIPELINE_STATE_FRAME_DONE       /**< Frame complete, cleaning up     */
} Pipeline_StateTypeDef;

/** @brief Frame parameters */
#define PIPELINE_WIDTH       160u
#define PIPELINE_HEIGHT      128u
#define PIPELINE_FRAME_SIZE  (PIPELINE_WIDTH * PIPELINE_HEIGHT * 2u)  /**< 40960 bytes */
#define PIPELINE_BUFFER_SIZE 640u                                     /**< 2 x 320B ping-pong */
#define PIPELINE_HALF_SIZE   (PIPELINE_BUFFER_SIZE / 2u)              /**< 320 bytes */

_Static_assert(PIPELINE_FRAME_SIZE % PIPELINE_HALF_SIZE == 0u,
              "PIPELINE_FRAME_SIZE must be multiple of PIPELINE_HALF_SIZE");
_Static_assert(PIPELINE_BUFFER_SIZE == PIPELINE_HALF_SIZE * 2u,
              "PIPELINE_BUFFER_SIZE must be 2x PIPELINE_HALF_SIZE");
_Static_assert(PIPELINE_FRAME_SIZE == 40960u,
              "PIPELINE_FRAME_SIZE mismatch (expected 160*128*2)");
_Static_assert(PIPELINE_HALF_SIZE == 320u,
              "PIPELINE_HALF_SIZE mismatch (expected 640/2)");

/** @brief  Initialize pipeline module (set state to IDLE) */
void                  Pipeline_Init(void);

/** @brief  Get current state */
Pipeline_StateTypeDef Pipeline_GetState(void);

/** @brief  Poll for non-blocking delay completion (call from main loop) */
void                  Pipeline_Poll(void);

#endif /* PIPELINE_H */
