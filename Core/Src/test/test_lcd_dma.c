/**
  * @file    test_lcd_dma.c
  * @brief   LCD DMA hardware test group implementation (SPI DMA + ping-pong)
  */
#include "test_lcd_dma.h"
#include "test_lcd_common.h"
#include "test_runner.h"
#include "st7735.h"
#include "dwt_delay.h"
#include "debug.h"
#include "unity.h"
#include "stm32f1xx_hal.h"
#include "main.h"

#include <stdbool.h>

extern SPI_HandleTypeDef hspi2;

/**
  * @brief   Wait for SPI DMA transfer to complete (poll SPI state)
  * @param   timeout_ms  Timeout in milliseconds
  * @retval  true   SPI returned to READY state within timeout
  * @retval  false  Timeout expired
  *
  *          Uses HAL_SPI_GetState polling instead of callback registration
  *          to avoid conflicting with pipeline.c's HAL_SPI_TxCpltCallback
  *          override and to not require USE_HAL_SPI_REGISTER_CALLBACKS.
  */
static bool WaitSpiReady(uint32_t timeout_ms)
{
  uint32_t deadline = HAL_GetTick() + timeout_ms;
  while (HAL_SPI_GetState(&hspi2) != HAL_SPI_STATE_READY)
  {
    if (HAL_GetTick() > deadline)
    {
      return false;
    }
  }
  return true;
}

/* ---- Unity test functions ---- */

void TestLcdDmaBasic(void)
{
  static uint8_t dma_buf[LCD_TEST_LINE_SIZE];

  /* Fill with red */
  for (uint16_t i = 0u; i < LCD_TEST_WIDTH; i++)
  {
    dma_buf[i * 2u]      = 0xF8u;
    dma_buf[i * 2u + 1u] = 0x00u;
  }

  LCD_SetAddrWindow(0u, 0u, 159u, 0u);  /* 1 line */

  HAL_StatusTypeDef status = HAL_SPI_Transmit_DMA(&hspi2, dma_buf,
                                                   LCD_TEST_LINE_SIZE);
  TEST_ASSERT_EQUAL(HAL_OK, status);
  TEST_ASSERT_TRUE(WaitSpiReady(100u));

  LCD_CS_High();
  TEST_PASS();
}

void TestLcdDmaFullFrame(void)
{
  static uint8_t dma_buf[LCD_TEST_LINE_SIZE];

  LCD_SetAddrWindow(0u, 0u, 159u, 127u);

  for (uint16_t y = 0u; y < LCD_TEST_HEIGHT; y++)
  {
    /* Fill with green */
    for (uint16_t i = 0u; i < LCD_TEST_WIDTH; i++)
    {
      dma_buf[i * 2u]      = 0x07u;
      dma_buf[i * 2u + 1u] = 0xE0u;
    }

    HAL_StatusTypeDef status = HAL_SPI_Transmit_DMA(&hspi2, dma_buf,
                                                     LCD_TEST_LINE_SIZE);
    TEST_ASSERT_EQUAL(HAL_OK, status);
    TEST_ASSERT_TRUE(WaitSpiReady(100u));
  }

  LCD_CS_High();
  TEST_PASS();
}

/* ---- DMA ping-pong visual loop ---- */

static uint8_t s_buf_a[LCD_TEST_LINE_SIZE];
static uint8_t s_buf_b[LCD_TEST_LINE_SIZE];

/**
  * @brief   Draw one frame using DMA ping-pong (double buffer)
  * @param   pattern_id  Pattern index (0-8)
  *
  *          Fills back buffer while DMA sends front buffer (overlap).
  *          Swaps buffers each line for true ping-pong.
  */
static void DrawFrameDma(uint8_t pattern_id)
{
  uint8_t digit = pattern_id + 1u;
  uint8_t *front = s_buf_a;
  uint8_t *back  = s_buf_b;

  LCD_SetAddrWindow(0u, 0u, LCD_TEST_WIDTH - 1u, LCD_TEST_HEIGHT - 1u);

  /* Line 0: fill front buffer, start DMA */
  LcdTest_FillLine(front, 0u, pattern_id);
  LcdTest_OverlayDigit(front, 0u, digit);
  LcdTest_OverlayText(front, 0u, 4u, 4u, "DMA", 1u);
  HAL_SPI_Transmit_DMA(&hspi2, front, LCD_TEST_LINE_SIZE);

  for (uint16_t y = 1u; y < LCD_TEST_HEIGHT; y++)
  {
    /* Fill back buffer while DMA sends front (overlap) */
    LcdTest_FillLine(back, y, pattern_id);
    LcdTest_OverlayDigit(back, y, digit);
    LcdTest_OverlayText(back, y, 4u, 4u, "DMA", 1u);

    /* Wait for front DMA to complete */
    WaitSpiReady(100u);

    /* Swap: start DMA on back, fill front next iteration */
    uint8_t *tmp = front;
    front = back;
    back = tmp;

    HAL_SPI_Transmit_DMA(&hspi2, front, LCD_TEST_LINE_SIZE);
  }

  /* Wait for last DMA */
  WaitSpiReady(100u);
  LCD_CS_High();
}

/* ---- Entry point ---- */

void RunLcdDmaTests(void)
{
  DWT_Init();
  LCD_Init();

  UNITY_BEGIN();
  RUN_TEST(TestLcdDmaBasic);
  RUN_TEST(TestLcdDmaFullFrame);
  g_test_failures += UNITY_END();

  /* Visual test loop: cycle through 9 patterns, 3 seconds each */
  for (;;)
  {
    for (uint8_t p = 0u; p < LCD_TEST_PATTERN_COUNT; p++)
    {
      DrawFrameDma(p);
      HAL_Delay(3000u);
    }
  }
}
