/**
  * @file    test_lcd_common.h
  * @brief   Shared LCD test utilities (font, pattern fill, digit overlay)
  */
#ifndef TEST_LCD_COMMON_H
#define TEST_LCD_COMMON_H

#include <stdint.h>

#define LCD_TEST_RED     0xF800u
#define LCD_TEST_GREEN   0x07E0u
#define LCD_TEST_BLUE    0x001Fu
#define LCD_TEST_WHITE   0xFFFFu
#define LCD_TEST_BLACK   0x0000u

#define LCD_TEST_WIDTH      160u
#define LCD_TEST_HEIGHT     120u
#define LCD_TEST_LINE_SIZE  (LCD_TEST_WIDTH * 2u)
#define LCD_TEST_PATTERN_COUNT  9u

#define LCD_TEST_FONT_SCALE    4u
#define LCD_TEST_FONT_SPACING  1u
#define LCD_TEST_FONT_WIDTH    5u
#define LCD_TEST_FONT_HEIGHT   7u
#define LCD_TEST_FONT_X_START  70u
#define LCD_TEST_FONT_Y_START  50u

void LcdTest_FillLine(uint8_t *buf, uint16_t y, uint8_t pattern_id);
void LcdTest_OverlayDigit(uint8_t *buf, uint16_t y, uint8_t digit);

/** @brief  Overlay a text label (digits and uppercase letters) on a line
  * @param  buf      Line buffer (LCD_TEST_WIDTH x RGB565)
  * @param  y        Current line index (0..LCD_TEST_HEIGHT-1)
  * @param  x_start  Top-left column of the label
  * @param  y_start  Top-left row of the label
  * @param  text     Null-terminated label (e.g. "DMA", "BLK")
  * @param  scale    Font scale factor (1 = 5x7 px, line width 1 px)
  */
void LcdTest_OverlayText(uint8_t *buf, uint16_t y, uint16_t x_start,
                         uint16_t y_start, const char *text, uint8_t scale);

#endif /* TEST_LCD_COMMON_H */
