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

/* 5x7 bitmap font for uppercase letters A-Z (182 bytes ROM) */
static const uint8_t kFontLetters[26][7] =
{
  {0x0Eu, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u},  /* A */
  {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x11u, 0x11u, 0x1Eu},  /* B */
  {0x0Eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0Eu},  /* C */
  {0x1Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1Eu},  /* D */
  {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x1Fu},  /* E */
  {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x10u},  /* F */
  {0x0Eu, 0x11u, 0x10u, 0x17u, 0x11u, 0x11u, 0x0Fu},  /* G */
  {0x11u, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u},  /* H */
  {0x0Eu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu},  /* I */
  {0x07u, 0x02u, 0x02u, 0x02u, 0x02u, 0x12u, 0x0Cu},  /* J */
  {0x11u, 0x12u, 0x14u, 0x18u, 0x14u, 0x12u, 0x11u},  /* K */
  {0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x1Fu},  /* L */
  {0x11u, 0x1Bu, 0x15u, 0x15u, 0x11u, 0x11u, 0x11u},  /* M */
  {0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u},  /* N */
  {0x0Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu},  /* O */
  {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x10u, 0x10u, 0x10u},  /* P */
  {0x0Eu, 0x11u, 0x11u, 0x11u, 0x15u, 0x12u, 0x0Du},  /* Q */
  {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x14u, 0x12u, 0x11u},  /* R */
  {0x0Fu, 0x10u, 0x10u, 0x0Eu, 0x01u, 0x01u, 0x1Eu},  /* S */
  {0x1Fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u},  /* T */
  {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu},  /* U */
  {0x11u, 0x11u, 0x11u, 0x11u, 0x0Au, 0x0Au, 0x04u},  /* V */
  {0x11u, 0x11u, 0x15u, 0x15u, 0x15u, 0x1Bu, 0x11u},  /* W */
  {0x11u, 0x0Au, 0x04u, 0x04u, 0x04u, 0x0Au, 0x11u},  /* X */
  {0x11u, 0x0Au, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u},  /* Y */
  {0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x1Fu}   /* Z */
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

/**
  * @brief   Get the 5x7 glyph row for a printable char
  * @param   c    Character ('0'-'9' or 'A'-'Z')
  * @param   row  Row index 0-6
  * @retval  Bitmap byte, or 0 if char/row out of range
  */
static uint8_t GetGlyphRow(char c, uint8_t row)
{
  if (c >= '0' && c <= '9')
  {
    return kFont5x7[(uint8_t)(c - '0')][row];
  }
  if (c >= 'A' && c <= 'Z')
  {
    return kFontLetters[(uint8_t)(c - 'A')][row];
  }
  return 0u;
}

/**
  * @brief   Overlay a single text label on one line buffer row
  * @param   buf      Line buffer
  * @param   y        Current line index
  * @param   x_start  Top-left column of the label
  * @param   y_start  Top-left row of the label
  * @param   text     Null-terminated label
  * @param   scale    Font scale factor (1 = 5x7 px, line width 1 px)
  *
  *          Draws a black background box behind the text and white font
  *          pixels. Supports '0'-'9','A'-'Z'.
  */
void LcdTest_OverlayText(uint8_t *buf, uint16_t y, uint16_t x_start,
                         uint16_t y_start, const char *text, uint8_t scale)
{
  uint16_t len = 0u;
  while (text[len] != '\0')
  {
    len++;
  }
  if (len == 0u)
  {
    return;
  }

  uint16_t char_step = (uint16_t)(LCD_TEST_FONT_WIDTH * scale + LCD_TEST_FONT_SPACING * scale);
  uint16_t text_w = (uint16_t)(len * char_step - LCD_TEST_FONT_SPACING * scale);

  /* Draw black background box (2px padding around text) */
  if (y >= (y_start - 2u) &&
      y <= (y_start + LCD_TEST_FONT_HEIGHT * scale + 1u))
  {
    for (uint16_t x = (x_start - 2u);
         x <= (x_start + text_w + 1u);
         x++)
    {
      SetPixel(buf, x, LCD_TEST_BLACK);
    }
  }

  /* Draw white font pixels */
  if (y >= y_start && y < (y_start + LCD_TEST_FONT_HEIGHT * scale))
  {
    uint8_t font_row = (uint8_t)((y - y_start) / scale);
    uint16_t x_off = x_start;

    for (uint16_t i = 0u; i < len; i++)
    {
      uint8_t row_data = GetGlyphRow(text[i], font_row);

      for (uint8_t fc = 0u; fc < LCD_TEST_FONT_WIDTH; fc++)
      {
        if ((row_data & (0x10u >> fc)) != 0u)
        {
          uint16_t px_start = x_off + (uint16_t)fc * scale;
          for (uint8_t s = 0u; s < scale; s++)
          {
            SetPixel(buf, px_start + s, LCD_TEST_WHITE);
          }
        }
      }
      x_off += char_step;
    }
  }
}

void LcdTest_OverlayDigit(uint8_t *buf, uint16_t y, uint8_t digit)
{
  char d[2];
  d[0] = (digit <= 9u) ? (char)('0' + digit) : '\0';
  d[1] = '\0';
  LcdTest_OverlayText(buf, y, LCD_TEST_FONT_X_START, LCD_TEST_FONT_Y_START,
                      d, LCD_TEST_FONT_SCALE);
}
