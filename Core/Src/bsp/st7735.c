/**
  * @file    st7735.c
  * @brief   ST7735 1.8" TFT LCD driver implementation
  */
#include "st7735.h"
#include "dwt_delay.h"
#include "debug.h"
#include "stm32f1xx_hal.h"

/* SPI2 handle (defined in main.c) */
extern SPI_HandleTypeDef hspi2;

/* ST7735 command set */
#define ST7735_CMD_SLPOUT   0x11u
#define ST7735_CMD_MADCTL   0x36u
#define ST7735_CMD_COLMOD   0x3Au
#define ST7735_CMD_INVOFF   0x20u
#define ST7735_CMD_NORON    0x13u
#define ST7735_CMD_DISPON   0x29u
#define ST7735_CMD_CASET    0x2Au
#define ST7735_CMD_RASET    0x2Bu
#define ST7735_CMD_RAMWR    0x2Cu
#define ST7735_CMD_RDID1   0xDAu
#define ST7735_RDID1_EXPECTED  0x7Cu

/*
 * MADCTL: D5=MV (landscape), D3=BGR
 *
 * Default 0x28 = MV=1 + BGR=1 (common for 1.8" ST7735 modules).
 * If image is upside down: toggle D7 (MY) -> 0xA8.
 * If red/blue swapped: toggle D3 (RGB) -> 0x20.
 */
#define ST7735_MADCTL_VAL   0xA0u  /* MY=1, MX=0, MV=1, BGR=0 */

#define ST7735_COLMOD_RGB565  0x05u

/**
  * @brief   Send command byte via SPI (DC=0)
  * @param   cmd  Command byte
  *
  *          Caller manages CS. DC is set low before transmit.
  */
static void WriteCmd(uint8_t cmd)
{
  LCD_DC_Low();
  HAL_SPI_Transmit(&hspi2, &cmd, 1u, HAL_MAX_DELAY);
}

/**
  * @brief   Send data bytes via SPI (DC=1)
  * @param   data  Pointer to data buffer
  * @param   len   Number of bytes to send
  *
  *          Caller manages CS. DC is set high before transmit.
  */
static void WriteData(const uint8_t *data, uint16_t len)
{
  LCD_DC_High();
  HAL_SPI_Transmit(&hspi2, data, len, HAL_MAX_DELAY);
}

/**
  * @brief   Read an 8-bit register from ST7735 via SPI half-duplex
  * @param   cmd  Register read command (e.g. 0xDA for RDID1)
  * @retval  Register value
  *
  *          Sequence: CS low -> DC low -> send cmd (BIDIOE=1) ->
  *          SPI switches to receive (BIDIOE=0) -> read 1 byte -> CS high.
  *          Requires SPI_DIRECTION_1LINE in MX_SPI2_Init.
  */
static uint8_t ReadReg(uint8_t cmd)
{
  uint8_t value = 0u;

  LCD_CS_Low();
  LCD_DC_Low();

  HAL_SPI_Transmit(&hspi2, &cmd, 1u, HAL_MAX_DELAY);
  HAL_SPI_Receive(&hspi2, &value, 1u, HAL_MAX_DELAY);

  LCD_CS_High();
  return value;
}

uint8_t LCD_ReadReg(uint8_t cmd)
{
  return ReadReg(cmd);
}

void LCD_ReadRegMulti(uint8_t cmd, uint8_t *data, uint16_t len)
{
  LCD_CS_Low();
  LCD_DC_Low();

  HAL_SPI_Transmit(&hspi2, &cmd, 1u, HAL_MAX_DELAY);
  HAL_SPI_Receive(&hspi2, data, len, HAL_MAX_DELAY);

  LCD_CS_High();
}

void LCD_WritePixels(const uint8_t *data, uint16_t len)
{
  HAL_SPI_Transmit(&hspi2, data, len, HAL_MAX_DELAY);
}

void LCD_Init(void)
{
  /* Backlight on */
  LCD_BL_On();

  /* Hardware reset: low >= 10us, then high */
  LCD_RESET_Low();
  DWT_DelayUs(10u);
  LCD_RESET_High();
  DWT_DelayMs(120u);

  /* Verify SPI communication by reading RDID1 */
  uint8_t id = ReadReg(ST7735_CMD_RDID1);
  debug_printf("LCD RDID1=0x%02X (expect 0x%02X)\n", id, ST7735_RDID1_EXPECTED);
  if (id != ST7735_RDID1_EXPECTED)
  {
    debug_printf("WARNING: RDID1 mismatch, continuing init anyway\n");
  }

  /* CS low for entire init sequence */
  LCD_CS_Low();

  WriteCmd(ST7735_CMD_SLPOUT);
  DWT_DelayMs(120u);

  uint8_t madctl = ST7735_MADCTL_VAL;
  WriteCmd(ST7735_CMD_MADCTL);
  WriteData(&madctl, 1u);

  uint8_t colmod = ST7735_COLMOD_RGB565;
  WriteCmd(ST7735_CMD_COLMOD);
  WriteData(&colmod, 1u);

  WriteCmd(ST7735_CMD_INVOFF);
  WriteCmd(ST7735_CMD_NORON);
  WriteCmd(ST7735_CMD_DISPON);

  /* CS high after init */
  LCD_CS_High();
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
  LCD_CS_Low();

  WriteCmd(ST7735_CMD_CASET);
  WriteData(caset, 4u);

  WriteCmd(ST7735_CMD_RASET);
  WriteData(raset, 4u);

  WriteCmd(ST7735_CMD_RAMWR);

  /* DC=1 for pixel data, CS stays low for SPI DMA stream */
  LCD_DC_High();
}
