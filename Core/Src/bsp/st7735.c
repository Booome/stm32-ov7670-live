/**
  * @file    st7735.c
  * @brief   ST7735 1.8" TFT LCD driver implementation
  */
#include "st7735.h"
#include "dwt_delay.h"
#include "main.h"
#include "stm32f1xx_hal.h"

/* SPI2 handle (defined in main.c) */
extern SPI_HandleTypeDef hspi2;

/* ST7735 command set */
#define ST7735_CMD_SLPOUT   0x11u
#define ST7735_CMD_MADCTL   0x36u
#define ST7735_CMD_COLMOD   0x3Au
#define ST7735_CMD_INVON    0x21u
#define ST7735_CMD_NORON    0x13u
#define ST7735_CMD_DISPON   0x29u
#define ST7735_CMD_CASET    0x2Au
#define ST7735_CMD_RASET    0x2Bu
#define ST7735_CMD_RAMWR    0x2Cu

/*
 * MADCTL: D5=MV (landscape), D3=BGR
 *
 * Default 0x28 = MV=1 + BGR=1 (common for 1.8" ST7735 modules).
 * If image is upside down: toggle D7 (MY) -> 0xA8.
 * If red/blue swapped: toggle D3 (RGB) -> 0x20.
 */
#define ST7735_MADCTL_VAL   0x28u

#define ST7735_COLMOD_RGB565  0x05u

/**
  * @brief   Send command byte via SPI (DC=0)
  * @param   cmd  Command byte
  *
  *          Caller manages CS. DC is set low before transmit.
  */
static void lcd_write_cmd(uint8_t cmd)
{
  HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
  HAL_SPI_Transmit(&hspi2, &cmd, 1u, HAL_MAX_DELAY);
}

/**
  * @brief   Send data bytes via SPI (DC=1)
  * @param   data  Pointer to data buffer
  * @param   len   Number of bytes to send
  *
  *          Caller manages CS. DC is set high before transmit.
  */
static void lcd_write_data(uint8_t *data, uint16_t len)
{
  HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
  HAL_SPI_Transmit(&hspi2, data, len, HAL_MAX_DELAY);
}

void LCD_Init(void)
{
  /* Backlight on */
  HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);

  /* Hardware reset: low >= 10us, then high */
  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_RESET);
  DWT_DelayUs(10u);
  HAL_GPIO_WritePin(LCD_RESET_GPIO_Port, LCD_RESET_Pin, GPIO_PIN_SET);
  DWT_DelayMs(120u);

  /* CS low for entire init sequence */
  HAL_GPIO_WritePin(LCD_SPI_CS_GPIO_Port, LCD_SPI_CS_Pin, GPIO_PIN_RESET);

  lcd_write_cmd(ST7735_CMD_SLPOUT);
  DWT_DelayMs(120u);

  uint8_t madctl = ST7735_MADCTL_VAL;
  lcd_write_cmd(ST7735_CMD_MADCTL);
  lcd_write_data(&madctl, 1u);

  uint8_t colmod = ST7735_COLMOD_RGB565;
  lcd_write_cmd(ST7735_CMD_COLMOD);
  lcd_write_data(&colmod, 1u);

  lcd_write_cmd(ST7735_CMD_INVON);
  lcd_write_cmd(ST7735_CMD_NORON);
  lcd_write_cmd(ST7735_CMD_DISPON);

  /* CS high after init */
  HAL_GPIO_WritePin(LCD_SPI_CS_GPIO_Port, LCD_SPI_CS_Pin, GPIO_PIN_SET);
}

void LCD_SetAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  uint8_t caset[4] =
  {
    (uint8_t)(x0 >> 8),
    (uint8_t)(x0 & 0xFFu),
    (uint8_t)(x1 >> 8),
    (uint8_t)(x1 & 0xFFu)
  };
  uint8_t raset[4] =
  {
    (uint8_t)(y0 >> 8),
    (uint8_t)(y0 & 0xFFu),
    (uint8_t)(y1 >> 8),
    (uint8_t)(y1 & 0xFFu)
  };

  /* CS low for entire address window + RAMWR sequence */
  HAL_GPIO_WritePin(LCD_SPI_CS_GPIO_Port, LCD_SPI_CS_Pin, GPIO_PIN_RESET);

  lcd_write_cmd(ST7735_CMD_CASET);
  lcd_write_data(caset, 4u);

  lcd_write_cmd(ST7735_CMD_RASET);
  lcd_write_data(raset, 4u);

  lcd_write_cmd(ST7735_CMD_RAMWR);

  /* DC=1 for pixel data, CS stays low for SPI DMA stream */
  HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_SET);
}
