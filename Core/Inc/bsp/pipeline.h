/**
  * @file    pipeline.h
  * @brief   Dual-DMA frame capture pipeline (Camera -> FIFO -> LCD)
  *
  *          Camera DMA (Circular) reads GPIOA->IDR into PipelineBuffer[640],
  *          SPI DMA (Normal) sends 320B half-buffers to ST7735 LCD.
  *
 *          Frame: 160x120 RGB565 = 38400 bytes = 120 x 320B half-buffers
 *          (centered on 160x128 LCD; crop macros trim edge artifacts)
 *          VSYNC triggered, DWT non-blocking half-frame read delay (15ms):
 *          read overlaps the next frame's write, so writing the next frame's
 *          front half overwrites FIFO addresses the reader already passed.
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
#include "periph_map.h"

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
  PIPELINE_STATE_DISABLED = 0,    /**< Not yet initialized          */
  PIPELINE_STATE_IDLE,            /**< Read idle, no DMA running    */
  PIPELINE_STATE_FRAME_CAPTURING, /**< Frame read in progress       */
  PIPELINE_STATE_FRAME_DONE       /**< Frame read complete          */
} Pipeline_StateTypeDef;

/** @brief Frame parameters */
#define PIPELINE_WIDTH       160u
#define PIPELINE_HEIGHT      120u
#define PIPELINE_LCD_WIDTH   160u
#define PIPELINE_LCD_HEIGHT  128u

/* Edge crop: discard camera-frame edge pixels to hide artifacts */
#define PIPELINE_CROP_TOP     1u
#define PIPELINE_CROP_BOTTOM  0u
#define PIPELINE_CROP_LEFT    6u
#define PIPELINE_CROP_RIGHT   1u

#define PIPELINE_DISP_WIDTH   (PIPELINE_WIDTH - PIPELINE_CROP_LEFT - PIPELINE_CROP_RIGHT)
#define PIPELINE_DISP_HEIGHT  (PIPELINE_HEIGHT - PIPELINE_CROP_TOP - PIPELINE_CROP_BOTTOM)
#define PIPELINE_ROW_BYTES    (PIPELINE_DISP_WIDTH * 2u)
#define PIPELINE_LCD_X_OFFSET ((PIPELINE_LCD_WIDTH - PIPELINE_DISP_WIDTH) / 2u)
#define PIPELINE_LCD_Y_OFFSET ((PIPELINE_LCD_HEIGHT - PIPELINE_DISP_HEIGHT) / 2u)

#define PIPELINE_FRAME_SIZE  (PIPELINE_WIDTH * PIPELINE_HEIGHT * 2u)  /**< 38400 bytes */
#define PIPELINE_BUFFER_SIZE 640u                                     /**< 2 x 320B ping-pong */
#define PIPELINE_HALF_SIZE   (PIPELINE_BUFFER_SIZE / 2u)              /**< 320 bytes */

_Static_assert(PIPELINE_FRAME_SIZE % PIPELINE_HALF_SIZE == 0u,
              "PIPELINE_FRAME_SIZE must be multiple of PIPELINE_HALF_SIZE");
_Static_assert(PIPELINE_BUFFER_SIZE == PIPELINE_HALF_SIZE * 2u,
              "PIPELINE_BUFFER_SIZE must be 2x PIPELINE_HALF_SIZE");
_Static_assert(PIPELINE_FRAME_SIZE == 38400u,
              "PIPELINE_FRAME_SIZE mismatch (expected 160*120*2)");
_Static_assert(PIPELINE_HALF_SIZE == 320u,
              "PIPELINE_HALF_SIZE mismatch (expected 640/2)");
_Static_assert(PIPELINE_CROP_LEFT + PIPELINE_CROP_RIGHT < PIPELINE_WIDTH,
              "horizontal crop must leave visible columns");
_Static_assert(PIPELINE_CROP_TOP + PIPELINE_CROP_BOTTOM < PIPELINE_HEIGHT,
              "vertical crop must leave visible rows");
_Static_assert(PIPELINE_ROW_BYTES <= PIPELINE_HALF_SIZE,
              "cropped row must fit one half-buffer");

/** @brief  Initialize pipeline module (set state to IDLE) */
void                  Pipeline_Init(void);

/** @brief  Get current state */
Pipeline_StateTypeDef Pipeline_GetState(void);

/** @brief  Get total frames captured since Pipeline_Init */
uint32_t Pipeline_GetFrameCount(void);

/** @brief  Poll for non-blocking delay completion (call from main loop) */
void                  Pipeline_Poll(void);

/** @brief  VSYNC EXTI (PA11) interrupt entry (LL, replaces HAL GPIO EXTI) */
void                  Pipeline_VsyncExtiIrq(void);

/** @brief  Camera DMA (DMA1_Channel3) HT/TC interrupt entry (LL) */
void                  Pipeline_CameraDmaIrq(void);

#endif /* PIPELINE_H */
