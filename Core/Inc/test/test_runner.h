/**
  * @file    test_runner.h
  * @brief   Unit test dispatcher entry point
  *
  *          TestRunner_Run() is called from main() when UNIT_TESTS_ENABLED
  *          is defined (i.e. any TEST_* option is ON). It runs the selected
  *          test groups via Unity and never returns.
  */
#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

/**
  * @brief  Run all enabled test groups, then loop forever with LED feedback.
  * @note   Never returns. Called from main() under #ifdef UNIT_TESTS_ENABLED.
  */
void TestRunner_Run(void);

#endif /* TEST_RUNNER_H */
