/**
  * @file    test_test_led.h
  * @brief   TEST_LED smoke test group
  *
  *          Blink TEST_LED at 1 Hz to verify the test framework infrastructure
  *          (compile, flash, main branch switch, TestRunner dispatch, GPIO).
  */
#ifndef TEST_TEST_LED_H
#define TEST_TEST_LED_H

/**
  * @brief  Run TEST_LED smoke test (blinks forever, never returns).
  */
void RunTestLedTests(void);

#endif /* TEST_TEST_LED_H */
