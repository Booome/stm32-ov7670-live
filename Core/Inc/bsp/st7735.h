/**
  * @file    st7735.h
  * @brief   ST7735 1.8" TFT LCD driver (160x128 RGB565)
  *
  *          SPI2 18MHz 4-line serial interface.
  *          Provides initialization and frame buffer write setup.
  *          Requires DWT_Init() and MX_SPI2_Init() before use.
  */
#ifndef ST7735_H
#define ST7735_H

#include <stdint.h>

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

#endif /* ST7735_H */
