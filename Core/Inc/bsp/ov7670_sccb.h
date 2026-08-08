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

/* ---- SCCB read status codes (test API) ---- */

/** @brief  Status codes for SCCB_ReadRegEx diagnostics */
typedef enum
{
  SCCB_READ_OK = 0,        /**< Transfer complete, data valid */
  SCCB_READ_BUS_BUSY,      /**< SDA stuck low / bus not idle */
  SCCB_READ_NACK_ADDR,     /**< Device address NACK (no response) */
  SCCB_READ_NACK_REG,      /**< Register address NACK */
  SCCB_READ_NACK_RADDR     /**< Read address NACK */
} SCCB_ReadStatusTypeDef;

/* ---- OV7670 read-only identity registers ---- */

#define SCCB_REG_PID    0x0Au   /**< Product ID (expected 0x76)    */
#define SCCB_REG_VER    0x0Bu   /**< Version ID (expected 0x73)    */
#define SCCB_REG_MIDH   0x1Cu   /**< Manufacturer ID high (0x7F)   */
#define SCCB_REG_MIDL   0x1Du   /**< Manufacturer ID low  (0xA2)   */

/* ---- Public API (production, unchanged) ---- */

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

/* ---- Test API (new, with full status diagnostics) ---- */

/** @brief  Read one byte from OV7670 register with status diagnostics
  * @param  reg_addr  Register address (0x00-0xFF)
  * @param  data      Pointer to store read byte (valid only on SCCB_READ_OK)
  * @retval SCCB_READ_OK          Transfer complete, data valid
  * @retval SCCB_READ_BUS_BUSY    SDA stuck low before transaction
  * @retval SCCB_READ_NACK_ADDR   Device address NACK
  * @retval SCCB_READ_NACK_REG    Register address NACK
  * @retval SCCB_READ_NACK_RADDR  Read address NACK
  */
SCCB_ReadStatusTypeDef SCCB_ReadRegEx(uint8_t reg_addr, uint8_t *data);

#endif /* OV7670_SCCB_H */
