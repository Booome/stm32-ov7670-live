/**
  * @file    st7735.h
  * @brief   ST7735 1.8" TFT LCD driver (160x128 RGB565)
  *
  *          SPI2 18MHz 4-line serial interface (Half-Duplex BIDIMODE).
  *          Provides initialization and frame buffer write setup.
  *          Requires DWT_Init() and MX_SPI2_Init() before use.
  */
#ifndef ST7735_H
#define ST7735_H

#include <stdint.h>
#include "main.h"
#include "stm32f1xx_hal.h"

/* ---- LCD pin control (static inline) ---- */

/** @brief Set DC low (command mode) */
static inline void LCD_DC_Low(void)
{
  HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
}

/** @brief Set DC high (data mode) */
static inline void LCD_DC_High(void)
{
  HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
}

/** @brief Select LCD (CS low) */
static inline void LCD_CS_Low(void)
{
  HAL_GPIO_WritePin(LCD_SPI_CS_GPIO_Port, LCD_SPI_CS_Pin, GPIO_PIN_RESET);
}

/** @brief Deselect LCD (CS high) */
static inline void LCD_CS_High(void)
{
  HAL_GPIO_WritePin(LCD_SPI_CS_GPIO_Port, LCD_SPI_CS_Pin, GPIO_PIN_SET);
}

/** @brief Turn on backlight (BL high) */
static inline void LCD_BL_On(void)
{
  HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);
}

/** @brief Assert LCD hardware reset (RESET low) */
static inline void LCD_RESET_Low(void)
{
  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_RESET);
}

/** @brief Release LCD hardware reset (RESET high) */
static inline void LCD_RESET_High(void)
{
  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
}

/* ---- Public API ---- */

/** @brief  Full LCD initialization: reset, register config, display on */
void    LCD_Init(void);

/** @brief  Set active drawing area and prepare for pixel stream
  * @param  x0  Start column (0-159)
  * @param  y0  Start row (0-127)
  * @param  x1  End column (0-159)
  * @param  y1  End row (0-127)
  *
  *          Sends CASET + RASET + RAMWR.
  *          Leaves CS low and DC high for subsequent SPI DMA pixel data.
  *          Caller must raise CS after frame transfer completes.
  */
void    LCD_SetAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/** @brief  Read an 8-bit register from ST7735 (STUB, always returns 0)
  * @param  cmd  Register read command (e.g. 0xDA for RDID1)
  * @retval Register value
  *
  *         STM32F1 half-duplex (BIDIMODE) readback is unreliable for
  *         ST7735: SCK free-runs in BIDIOE=0 RX mode and the panel's
  *         tri-state timing requirement is missed, so reads return
  *         pin residue instead of register data. Kept as a stub so
  *         callers keep compiling; not usable for verification.
  */
uint8_t  LCD_ReadReg(uint8_t cmd);

/** @brief  Read multiple bytes from ST7735 register (STUB, no-op)
  * @param  cmd   Read command (e.g. 0x0B for RDD_MADCTL)
  * @param  data  Buffer to store received bytes (unused)
  * @param  len   Number of bytes to read (unused)
  * @note   See LCD_ReadReg() caveat: half-duplex readback is unreliable.
  */
void    LCD_ReadRegMulti(uint8_t cmd, uint8_t *data, uint16_t len);

/** @brief  Send pixel data to LCD via SPI (blocking)
  * @param  data  RGB565 pixel data
  * @param  len   Number of bytes to send
  * @note   Caller must call LCD_SetAddrWindow() first (leaves CS low, DC high).
  *         Caller raises CS after all pixel data is sent.
  */
void    LCD_WritePixels(const uint8_t *data, uint16_t len);

#endif /* ST7735_H */
