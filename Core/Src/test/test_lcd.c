/**
  * @file    test_lcd.c
  * @brief   LCD hardware test group implementation (blocking SPI)
  */
#include "test_lcd.h"
#include "test_lcd_common.h"
#include "st7735.h"
#include "dwt_delay.h"
#include "debug.h"
#include "unity.h"
#include "stm32f1xx_hal.h"
#include "main.h"

/* ---- Unity test functions ---- */

void test_lcd_rdid1(void)
{
  TEST_ASSERT_EQUAL(0x7Cu, LCD_ReadReg(0xDAu));
}

void test_lcd_rdid2_rdid3(void)
{
  uint8_t rdid2 = LCD_ReadReg(0xDBu);
  uint8_t rdid3 = LCD_ReadReg(0xDCu);
  TEST_ASSERT_NOT_EQUAL(0x00u, rdid2);
  TEST_ASSERT_NOT_EQUAL(0xFFu, rdid2);
  TEST_ASSERT_NOT_EQUAL(0x00u, rdid3);
  TEST_ASSERT_NOT_EQUAL(0xFFu, rdid3);
}

void test_lcd_madctl(void)
{
  uint8_t buf[2];
  LCD_ReadRegMulti(0x0Bu, buf, 2u);
  TEST_ASSERT_EQUAL(0x28u, buf[1]);
}

void test_lcd_colmod(void)
{
  uint8_t buf[2];
  LCD_ReadRegMulti(0x0Cu, buf, 2u);
  TEST_ASSERT_EQUAL(0x05u, buf[1]);
}

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
  RUN_TEST(test_lcd_rdid1);
  RUN_TEST(test_lcd_rdid2_rdid3);
  RUN_TEST(test_lcd_madctl);
  RUN_TEST(test_lcd_colmod);
  RUN_TEST(test_lcd_addr_window);
  UNITY_END();

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
