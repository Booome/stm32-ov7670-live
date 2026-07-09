/**
  * @file    camera.h
  * @brief   Frame capture state machine and DMA pipeline
  *
  *          Dual-DMA ping-pong pipeline:
  *          Camera DMA (Circular) reads GPIOA->IDR into CameraBuffer[640],
  *          SPI DMA (Normal) sends 320B half-buffers to ST7735 LCD.
  *
  *          Frame: 160x128 RGB565 = 40960 bytes = 128 x 320B half-buffers
  *          VSYNC triggered, DWT non-blocking 2ms delay before read start.
  *
  *          Requires: DWT_Init, SCCB_Init, OV7670_Init, LCD_Init,
  *          MX_TIM3_Init, MX_SPI2_Init, MX_DMA_Init before Camera_Init.
  */
#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>
#include <stdbool.h>
#include "dwt_delay.h"

/** @brief Camera frame capture states */
typedef enum
{
  CAMERA_STATE_IDLE = 0,        /**< Waiting for VSYNC              */
  CAMERA_STATE_FRAME_START,     /**< VSYNC received, waiting 2ms    */
  CAMERA_STATE_FRAME_CAPTURING, /**< DMA pipeline active            */
  CAMERA_STATE_FRAME_DONE       /**< Frame complete, cleaning up     */
} Camera_StateTypeDef;

/** @brief Frame parameters */
#define CAMERA_WIDTH       160u
#define CAMERA_HEIGHT      128u
#define CAMERA_FRAME_SIZE  (CAMERA_WIDTH * CAMERA_HEIGHT * 2u)  /**< 40960 bytes */
#define CAMERA_BUFFER_SIZE 640u                                 /**< 2 x 320B ping-pong */
#define CAMERA_HALF_SIZE   (CAMERA_BUFFER_SIZE / 2u)            /**< 320 bytes */

/** @brief  Initialize camera module (set state to IDLE) */
void              Camera_Init(void);

/** @brief  Get current state */
Camera_StateTypeDef Camera_GetState(void);

/** @brief  Poll for non-blocking delay completion (call from main loop) */
void              Camera_Poll(void);

/** @brief  VSYNC interrupt handler (call from HAL_GPIO_EXTI_Callback) */
void              Camera_OnVsync(void);

/** @brief  Camera DMA half-transfer callback (call from HAL_DMA_XferHalfCpltCallback) */
void              Camera_OnDmaHalfCplt(void);

/** @brief  Camera DMA transfer-complete callback (call from HAL_DMA_XferCpltCallback) */
void              Camera_OnDmaCplt(void);

/** @brief  SPI DMA transfer-complete callback (call from HAL_SPI_TxCpltCallback) */
void              Camera_OnSpiDmaCplt(void);

#endif /* CAMERA_H */
