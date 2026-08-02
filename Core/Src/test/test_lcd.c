/**
  * @file    test_lcd.c
  * @brief   LCD hardware test group implementation (blocking SPI)
  */
#include "test_lcd.h"
#include "test_lcd_common.h"
#include "test_runner.h"
#include "st7735.h"
#include "dwt_delay.h"
#include "unity.h"
#include "stm32f1xx_hal.h"
#include "main.h"

/* ---- Unity test functions ---- */

void test_lcd_addr_window(void)
{
  uint8_t pixel[2] = {0xF8u, 0x00u};  /* red */

  LCD_SetAddrWindow(0u, 0u, 159u, 127u);
  LCD_WritePixels(pixel, 2u);
  LCD_CS_High();

  LCD_SetAddrWindow(0u, 0u, 0u, 0u);
  LCD_WritePixels(pixel, 2u);
  LCD_CS_High();

  LCD_SetAddrWindow(159u, 127u, 159u, 127u);
  LCD_WritePixels(pixel, 2u);
  LCD_CS_High();

  TEST_PASS();
}

/* ---- Visual test loop ---- */

static uint8_t s_line_buf[LCD_TEST_LINE_SIZE];

static void DrawFrameBlocking(uint8_t pattern_id)
{
  uint8_t digit = pattern_id + 1u;

  LCD_SetAddrWindow(0u, 0u, LCD_TEST_WIDTH - 1u, LCD_TEST_HEIGHT - 1u);

  for (uint16_t y = 0u; y < LCD_TEST_HEIGHT; y++)
  {
    LcdTest_FillLine(s_line_buf, y, pattern_id);
    LcdTest_OverlayDigit(s_line_buf, y, digit);
    LcdTest_OverlayText(s_line_buf, y, 4u, 4u, "BLK", 1u);
    LCD_WritePixels(s_line_buf, LCD_TEST_LINE_SIZE);
  }

  LCD_CS_High();
}

/* ---- Entry point ---- */

void RunLcdTests(void)
{
  DWT_Init();
  LCD_Init();

  UNITY_BEGIN();
  RUN_TEST(test_lcd_addr_window);
  g_test_failures += UNITY_END();

  /* Visual test loop: cycle through 9 patterns, 3 seconds each */
  for (;;)
  {
    for (uint8_t p = 0u; p < LCD_TEST_PATTERN_COUNT; p++)
    {
      DrawFrameBlocking(p);
      HAL_Delay(3000u);
    }
  }
}
