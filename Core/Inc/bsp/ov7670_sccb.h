/**
  * @file    ov7670_sccb.h
  * @brief   SCCB (I2C-compatible) bit-bang driver for OV7670
  *
  *          GPIO bit-bang implementation of the SCCB protocol for
  *          OV7670 camera register configuration. Uses DWT CYCCNT
  *          for cycle-accurate timing (requires DWT_Init() before use).
  *
  *          Bus: SCL=PB10, SDA=PB11 (both open-drain, external pull-up)
  *          Clock: ~400 kHz (SCCB spec maximum)
  */
#ifndef OV7670_SCCB_H
#define OV7670_SCCB_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "stm32f1xx_hal.h"

/* ---- SCCB pin control (static inline, OD output) ---- */

/** @brief Release SCL (high via pull-up) */
static inline void SCCB_SCL_High(void)
{
  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_SET);
}

/** @brief Drive SCL low */
static inline void SCCB_SCL_Low(void)
{
  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_RESET);
}

/** @brief Release SDA (high via pull-up) */
static inline void SCCB_SDA_High(void)
{
  HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_SET);
}

/** @brief Drive SDA low */
static inline void SCCB_SDA_Low(void)
{
  HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_RESET);
}

/** @brief Read SDA pin state */
static inline GPIO_PinState SCCB_SDA_Read(void)
{
  return HAL_GPIO_ReadPin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin);
}

/* ---- Public API ---- */

/** @brief  Initialize SCCB bus (set pins idle high) */
void    SCCB_Init(void);

/** @brief  Write one byte to OV7670 register
  * @param  reg_addr  Register address (0x00-0xFF)
  * @param  data      Byte to write
  * @retval true   ACK received (slave responded)
  * @retval false  NACK or bus error
  */
bool    SCCB_WriteReg(uint8_t reg_addr, uint8_t data);

/** @brief  Read one byte from OV7670 register
  * @param  reg_addr  Register address (0x00-0xFF)
  * @return Byte read from register (0x00 if bus error)
  */
uint8_t SCCB_ReadReg(uint8_t reg_addr);

#endif /* OV7670_SCCB_H */
