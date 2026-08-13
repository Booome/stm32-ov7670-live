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
#define ST7735_CMD_FRMCTR1  0xB1u
#define ST7735_CMD_FRMCTR2  0xB2u
#define ST7735_CMD_FRMCTR3  0xB3u
#define ST7735_CMD_INVCTR   0xB4u
#define ST7735_CMD_PWCTR1   0xC0u
#define ST7735_CMD_PWCTR2   0xC1u
#define ST7735_CMD_PWCTR3   0xC2u
#define ST7735_CMD_PWCTR4   0xC3u
#define ST7735_CMD_PWCTR5   0xC4u
#define ST7735_CMD_VMCTR1   0xC5u
#define ST7735_CMD_GMCTRP1  0xE0u
#define ST7735_CMD_GMCTRN1  0xE1u
#define ST7735_CMD_MADCTL   0x36u
#define ST7735_CMD_COLMOD   0x3Au
#define ST7735_CMD_INVOFF   0x20u
#define ST7735_CMD_NORON    0x13u
#define ST7735_CMD_DISPON   0x29u
#define ST7735_CMD_CASET    0x2Au
#define ST7735_CMD_RASET    0x2Bu
#define ST7735_CMD_RAMWR    0x2Cu

/*
 * Full init sequence used for ST7735S-compatible panels.
 * These set frame rate, power/charge-pump levels, VCOM and the
 * positive/negative gamma curves. On compatible controllers some
 * registers may be ignored (harmless); gamma config usually helps
 * balance red sub-pixel brightness which is low by default.
 */
static const uint8_t ST7735_INIT_SEQ[][18] =
{
  {ST7735_CMD_FRMCTR1, 3u, 0x01u, 0x2Cu, 0x2Du},
  {ST7735_CMD_FRMCTR2, 3u, 0x01u, 0x2Cu, 0x2Du},
  {ST7735_CMD_FRMCTR3, 6u, 0x01u, 0x2Cu, 0x2Du, 0x01u, 0x2Cu, 0x2Du},
  {ST7735_CMD_INVCTR,  1u, 0x07u},
  {ST7735_CMD_PWCTR1,  3u, 0xA2u, 0x02u, 0x84u},
  {ST7735_CMD_PWCTR2,  1u, 0xC5u},
  {ST7735_CMD_PWCTR3,  2u, 0x0Au, 0x00u},
  {ST7735_CMD_PWCTR4,  2u, 0x8Au, 0x2Au},
  {ST7735_CMD_PWCTR5,  2u, 0x8Au, 0xEEu},
  {ST7735_CMD_VMCTR1,  1u, 0x0Eu},
  {ST7735_CMD_GMCTRP1, 16u,
   0x02u, 0x1Cu, 0x07u, 0x12u, 0x37u, 0x32u, 0x29u, 0x2Du,
   0x29u, 0x25u, 0x2Bu, 0x39u, 0x00u, 0x01u, 0x03u, 0x10u},
  {ST7735_CMD_GMCTRN1, 16u,
   0x03u, 0x1Du, 0x07u, 0x06u, 0x2Eu, 0x2Cu, 0x29u, 0x2Du,
   0x2Eu, 0x2Eu, 0x37u, 0x3Fu, 0x00u, 0x00u, 0x02u, 0x10u},
};

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
  *         NOTE: STM32F1 half-duplex readback is unreliable for ST7735
  *         (SCK free-runs in BIDIOE=0 RX mode and tri-state timing is
  *         missed). Kept as stub; do not use for verification.
  */
uint8_t LCD_ReadReg(uint8_t cmd)
{
  (void)cmd;
  return 0u;
}

void LCD_ReadRegMulti(uint8_t cmd, uint8_t *data, uint16_t len)
{
  (void)cmd;
  (void)data;
  (void)len;
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

  /* CS low for entire init sequence */
  LCD_CS_Low();

  WriteCmd(ST7735_CMD_SLPOUT);
  DWT_DelayMs(120u);

  /* Full panel init: frame rate, power, VCOM and gamma curves */
  for (size_t i = 0u; i < (sizeof(ST7735_INIT_SEQ) / sizeof(ST7735_INIT_SEQ[0])); i++)
  {
    WriteCmd(ST7735_INIT_SEQ[i][0]);
    WriteData(&ST7735_INIT_SEQ[i][2u], ST7735_INIT_SEQ[i][1]);
  }

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

  /* Clear power-on GRAM garbage so the blank border rows stay black */
  LCD_FillScreen(0x0000u);
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

void LCD_FillScreen(uint16_t color)
{
  static uint8_t row[LCD_WIDTH * 2u];
  uint16_t x;
  uint16_t y;

  for (x = 0u; x < LCD_WIDTH; x++)
  {
    row[x * 2u]     = (uint8_t)(color >> 8);
    row[x * 2u + 1u] = (uint8_t)(color & 0xFFu);
  }

  LCD_SetAddrWindow(0u, 0u, LCD_WIDTH - 1u, LCD_HEIGHT - 1u);

  for (y = 0u; y < LCD_HEIGHT; y++)
  {
    HAL_SPI_Transmit(&hspi2, row, LCD_WIDTH * 2u, HAL_MAX_DELAY);
  }

  LCD_CS_High();
}
