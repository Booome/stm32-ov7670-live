/**
  * @file    ov7670.h
  * @brief   OV7670 camera register configuration driver
  *
  *          Handles hardware power-up sequence (PWDN/RESET) and
  *          SCCB register configuration for 160x128 RGB565 output.
  *          Depends on SCCB module (requires SCCB_Init before use).
  */
#ifndef OV7670_H
#define OV7670_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "stm32f1xx_hal.h"

/* ---- OV7670 pin control (static inline) ---- */

/** @brief Power down camera (PWDN high) */
static inline void OV7670_PWDN_High(void)
{
  HAL_GPIO_WritePin(OV7670_PWDN_GPIO_Port, OV7670_PWDN_Pin, GPIO_PIN_SET);
}

/** @brief Power on camera (PWDN low) */
static inline void OV7670_PWDN_Low(void)
{
  HAL_GPIO_WritePin(OV7670_PWDN_GPIO_Port, OV7670_PWDN_Pin, GPIO_PIN_RESET);
}

/** @brief Assert camera reset (RESET low) */
static inline void OV7670_RESET_Low(void)
{
  HAL_GPIO_WritePin(OV7670_RESET_GPIO_Port, OV7670_RESET_Pin, GPIO_PIN_RESET);
}

/** @brief Release camera reset (RESET high) */
static inline void OV7670_RESET_High(void)
{
  HAL_GPIO_WritePin(OV7670_RESET_GPIO_Port, OV7670_RESET_Pin, GPIO_PIN_SET);
}

/** @brief Assert FIFO write pointer reset (WRST low) */
static inline void OV7670_FIFO_WRST_Low(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_WRST_GPIO_Port, OV7670_FIFO_WRST_Pin, GPIO_PIN_RESET);
}

/** @brief Release FIFO write pointer reset (WRST high) */
static inline void OV7670_FIFO_WRST_High(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_WRST_GPIO_Port, OV7670_FIFO_WRST_Pin, GPIO_PIN_SET);
}

/** @brief Assert FIFO read pointer reset (RRST low) */
static inline void OV7670_FIFO_RRST_Low(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_RRST_GPIO_Port, OV7670_FIFO_RRST_Pin, GPIO_PIN_RESET);
}

/** @brief Release FIFO read pointer reset (RRST high) */
static inline void OV7670_FIFO_RRST_High(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_RRST_GPIO_Port, OV7670_FIFO_RRST_Pin, GPIO_PIN_SET);
}

/** @brief Enable FIFO output (OE low) */
static inline void OV7670_FIFO_OE_Low(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_OE_GPIO_Port, OV7670_FIFO_OE_Pin, GPIO_PIN_RESET);
}

/** @brief Disable FIFO output (OE high) */
static inline void OV7670_FIFO_OE_High(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_OE_GPIO_Port, OV7670_FIFO_OE_Pin, GPIO_PIN_SET);
}

/** @brief Enable FIFO write (WR high, NAND gate active) */
static inline void OV7670_FIFO_WR_High(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_WR_GPIO_Port, OV7670_FIFO_WR_Pin, GPIO_PIN_SET);
}

/** @brief Disable FIFO write (WR low) */
static inline void OV7670_FIFO_WR_Low(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_WR_GPIO_Port, OV7670_FIFO_WR_Pin, GPIO_PIN_RESET);
}

/* ---- Public API ---- */

/** @brief  Initialize OV7670: hardware reset + SCCB register config
  * @retval true   All registers written successfully
  * @retval false  Camera not responding (SCCB NACK)
  */
bool    OV7670_Init(void);

/** @brief  Enable color bar test pattern (COM3 bit0 = 1) */
void    OV7670_EnableColorBar(void);

/** @brief  Disable color bar test pattern (COM3 bit0 = 0) */
void    OV7670_DisableColorBar(void);

#endif /* OV7670_H */
