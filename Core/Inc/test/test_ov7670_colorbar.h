/**
  * @file    test_ov7670_colorbar.h
  * @brief   TEST_OV7670_COLORBAR group - colorbar pixel data verification
  *
  *          Self-contained group: performs its own OV7670_Init + colorbar
  *          enable, then manually reads the AL422B FIFO via GPIO bit-banged
  *          RCK to verify each 320B line is an 8-bar colorbar pattern.
  */
#ifndef TEST_OV7670_COLORBAR_H
#define TEST_OV7670_COLORBAR_H

/** @brief  Run all OV7670 colorbar data verification tests */
void RunOv7670ColorbarTests(void);

#endif /* TEST_OV7670_COLORBAR_H */
