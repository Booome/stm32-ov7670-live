/**
  * @file    test_lcd_common.c
  * @brief   Shared LCD test utilities implementation
  */
#include "test_lcd_common.h"

/* 5x7 bitmap font for digits 0-9 (70 bytes ROM) */
static const uint8_t kFont5x7[10][7] =
{
  {0x0Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu},  /* 0 */
  {0x04u, 0x0Cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu},  /* 1 */
  {0x0Eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1Fu},  /* 2 */
  {0x1Fu, 0x02u, 0x04u, 0x02u, 0x01u, 0x11u, 0x0Eu},  /* 3 */
  {0x02u, 0x06u, 0x0Au, 0x12u, 0x1Fu, 0x02u, 0x02u},  /* 4 */
  {0x1Fu, 0x10u, 0x1Eu, 0x01u, 0x01u, 0x11u, 0x0Eu},  /* 5 */
  {0x06u, 0x08u, 0x10u, 0x1Eu, 0x11u, 0x11u, 0x0Eu},  /* 6 */
  {0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u},  /* 7 */
  {0x0Eu, 0x11u, 0x11u, 0x0Eu, 0x11u, 0x11u, 0x0Eu},  /* 8 */
  {0x0Eu, 0x11u, 0x11u, 0x0Fu, 0x01u, 0x02u, 0x0Cu}   /* 9 */
};

/**
  * @brief   Write a 16-bit color into line buffer at pixel x (big-endian RGB565)
  */
static void SetPixel(uint8_t *buf, uint16_t x, uint16_t color)
{
  buf[x * 2u]      = (uint8_t)(color >> 8);
  buf[x * 2u + 1u] = (uint8_t)(color & 0xFFu);
}

/**
  * @brief   Fill entire line with one color
  */
static void FillSolid(uint8_t *buf, uint16_t color)
{
  for (uint16_t x = 0u; x < LCD_TEST_WIDTH; x++)
  {
    SetPixel(buf, x, color);
  }
}

void LcdTest_FillLine(uint8_t *buf, uint16_t y, uint8_t pattern_id)
{
  switch (pattern_id)
  {
    case 0u:  /* solid red */
      FillSolid(buf, LCD_TEST_RED);
      break;

    case 1u:  /* solid green */
      FillSolid(buf, LCD_TEST_GREEN);
      break;

    case 2u:  /* solid blue */
      FillSolid(buf, LCD_TEST_BLUE);
      break;

    case 3u:  /* solid white */
      FillSolid(buf, LCD_TEST_WHITE);
      break;

    case 4u:  /* solid black */
      FillSolid(buf, LCD_TEST_BLACK);
      break;

    case 5u:  /* horizontal stripes: 5 bands, ~26 rows each */
    {
      static const uint16_t colors[] =
      {
        LCD_TEST_RED, LCD_TEST_GREEN, LCD_TEST_BLUE,
        LCD_TEST_WHITE, LCD_TEST_BLACK
      };
      uint8_t band = (uint8_t)(y / 26u);
      if (band > 4u)
      {
        band = 4u;
      }
      FillSolid(buf, colors[band]);
      break;
    }

    case 6u:  /* vertical stripes: 5 bands, 32 columns each */
    {
      static const uint16_t colors[] =
      {
        LCD_TEST_RED, LCD_TEST_GREEN, LCD_TEST_BLUE,
        LCD_TEST_WHITE, LCD_TEST_BLACK
      };
      for (uint16_t x = 0u; x < LCD_TEST_WIDTH; x++)
      {
        uint8_t band = (uint8_t)(x / 32u);
        if (band > 4u)
        {
          band = 4u;
        }
        SetPixel(buf, x, colors[band]);
      }
      break;
    }

    case 7u:  /* checkerboard: 20x20 blocks, red/white */
    {
      uint8_t row_blk = (uint8_t)(y / 20u);
      for (uint16_t x = 0u; x < LCD_TEST_WIDTH; x++)
      {
        uint8_t col_blk = (uint8_t)(x / 20u);
        if (((row_blk + col_blk) & 1u) != 0u)
        {
          SetPixel(buf, x, LCD_TEST_WHITE);
        }
        else
        {
          SetPixel(buf, x, LCD_TEST_RED);
        }
      }
      break;
    }

    case 8u:  /* RGB gradient: 3 horizontal bands */
    {
      uint8_t band = (uint8_t)(y / 43u);  /* 128/3 ~= 43 */
      if (band > 2u)
      {
        band = 2u;
      }
      for (uint16_t x = 0u; x < LCD_TEST_WIDTH; x++)
      {
        uint16_t color = 0u;
        if (band == 0u)
        {
          /* red gradient: 0..31 over 160 pixels */
          color = (uint16_t)((x * 31u / 159u) << 11);
        }
        else if (band == 1u)
        {
          /* green gradient: 0..63 over 160 pixels */
          color = (uint16_t)((x * 63u / 159u) << 5);
        }
        else
        {
          /* blue gradient: 0..31 over 160 pixels */
          color = (uint16_t)(x * 31u / 159u);
        }
        SetPixel(buf, x, color);
      }
      break;
    }

    default:
      FillSolid(buf, LCD_TEST_BLACK);
      break;
  }
}

void LcdTest_OverlayDigit(uint8_t *buf, uint16_t y, uint8_t digit)
{
  if (digit > 9u)
  {
    return;
  }

  /* Draw black background box (2px padding around font) */
  if (y >= (LCD_TEST_FONT_Y_START - 2u) &&
      y <= (LCD_TEST_FONT_Y_START + LCD_TEST_FONT_HEIGHT * LCD_TEST_FONT_SCALE + 1u))
  {
    for (uint16_t x = (LCD_TEST_FONT_X_START - 2u);
         x <= (LCD_TEST_FONT_X_START + LCD_TEST_FONT_WIDTH * LCD_TEST_FONT_SCALE + 1u);
         x++)
    {
      SetPixel(buf, x, LCD_TEST_BLACK);
    }
  }

  /* Draw white font pixels */
  if (y >= LCD_TEST_FONT_Y_START &&
      y < (LCD_TEST_FONT_Y_START + LCD_TEST_FONT_HEIGHT * LCD_TEST_FONT_SCALE))
  {
    uint8_t font_row = (uint8_t)((y - LCD_TEST_FONT_Y_START) / LCD_TEST_FONT_SCALE);
    uint8_t row_data = kFont5x7[digit][font_row];

    for (uint8_t fc = 0u; fc < LCD_TEST_FONT_WIDTH; fc++)
    {
      if ((row_data & (0x10u >> fc)) != 0u)
      {
        uint16_t x_start = LCD_TEST_FONT_X_START + (uint16_t)fc * LCD_TEST_FONT_SCALE;
        for (uint8_t s = 0u; s < LCD_TEST_FONT_SCALE; s++)
        {
          SetPixel(buf, x_start + s, LCD_TEST_WHITE);
        }
      }
    }
  }
}
