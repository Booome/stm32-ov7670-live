/**
  * @file    test_test_led.c
  * @brief   TEST_LED smoke test implementation
  */
#include "test_test_led.h"
#include "stm32f1xx_hal.h"
#include "main.h"

void RunTestLedTests(void)
{
  /* Smoke test: blink TEST_LED at 1 Hz forever.
     Seeing the LED blink confirms the entire test pipeline is alive:
     build, flash, main() branch switch, TestRunner dispatch, GPIO toggle. */
  for (;;)
  {
    HAL_GPIO_TogglePin(TEST_LED_GPIO_Port, TEST_LED_Pin);
    HAL_Delay(500u);
  }
}
