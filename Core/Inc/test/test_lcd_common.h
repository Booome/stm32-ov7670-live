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
#define LCD_TEST_HEIGHT     128u
#define LCD_TEST_LINE_SIZE  (LCD_TEST_WIDTH * 2u)
#define LCD_TEST_PATTERN_COUNT  9u

#define LCD_TEST_FONT_SCALE    4u
#define LCD_TEST_FONT_WIDTH    5u
#define LCD_TEST_FONT_HEIGHT   7u
#define LCD_TEST_FONT_X_START  70u
#define LCD_TEST_FONT_Y_START  50u

void LcdTest_FillLine(uint8_t *buf, uint16_t y, uint8_t pattern_id);
void LcdTest_OverlayDigit(uint8_t *buf, uint16_t y, uint8_t digit);

#endif /* TEST_LCD_COMMON_H */
