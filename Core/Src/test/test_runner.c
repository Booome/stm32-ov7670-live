/**
  * @file    test_runner.c
  * @brief   Unit test dispatcher - runs enabled groups via Unity
  */
#include "test_runner.h"
#include "unity.h"
#include "debug.h"
#include "stm32f1xx_hal.h"
#include "main.h"

#ifdef TEST_TEST_LED
#include "test_test_led.h"
#endif
#ifdef TEST_LOGIC
#include "test_logic.h"
#endif
#ifdef TEST_SCCB
#include "test_sccb.h"
#endif
#ifdef TEST_OV7670
#include "test_ov7670.h"
#endif
#ifdef TEST_OV7670_COLORBAR
#include "test_ov7670_colorbar.h"
#endif
#ifdef TEST_OV7670_COLORBAR_DMA
#include "test_ov7670_colorbar_dma.h"
#endif
#ifdef TEST_LCD
#include "test_lcd.h"
#endif
#ifdef TEST_LCD_DMA
#include "test_lcd_dma.h"
#endif
#ifdef TEST_PIPELINE
#include "test_pipeline.h"
#endif

/* Unity requires these hooks; empty by default.
   Hardware init is done per-group in each RunXxxTests() entry. */
void setUp(void)
{
}

void tearDown(void)
{
}

/* Accumulated across groups; see test_runner.h */
volatile int g_test_failures = 0;

void TestRunner_Run(void)
{
  /* Emit blank lines to separate from previous session output */
  debug_printf("\n\n\n");
  debug_printf("=== Unit Test Runner Begin ===\n");

  /* Each RunXxxTests() owns its own UNITY_BEGIN()/UNITY_END();
     UnityBegin/UnityEnd are not reentrant and reset test state. */
#ifdef TEST_TEST_LED
  RunTestLedTests();   /* never returns: blinks TEST_LED at 1 Hz */
#endif
#ifdef TEST_LOGIC
  RunLogicTests();
#endif
#ifdef TEST_SCCB
  RunSccbTests();
#endif
#ifdef TEST_OV7670
  RunOv7670Tests();
#endif
#ifdef TEST_OV7670_COLORBAR
  RunOv7670ColorbarTests();
#endif
#ifdef TEST_OV7670_COLORBAR_DMA
  RunOv7670ColorbarDmaTests();
#endif
#ifdef TEST_LCD
  RunLcdTests();
#endif
#ifdef TEST_LCD_DMA
  RunLcdDmaTests();
#endif
#ifdef TEST_PIPELINE
  RunPipelineTests();   /* never returns: colorbar -> LCD streaming */
#endif

  debug_printf("=== Unit Test Runner End ===\n");

  /* TEST_LED feedback: all pass -> steady on, any fail -> fast blink. */
  for (;;)
  {
    if (g_test_failures == 0)
    {
      HAL_GPIO_WritePin(TEST_LED_GPIO_Port, TEST_LED_Pin, GPIO_PIN_SET);
    }
    else
    {
      HAL_GPIO_TogglePin(TEST_LED_GPIO_Port, TEST_LED_Pin);
      HAL_Delay(150u);
    }
  }
}
