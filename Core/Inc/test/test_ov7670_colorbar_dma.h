/**
  * @file    test_ov7670_colorbar_dma.h
  * @brief   TEST_OV7670_COLORBAR_DMA group - colorbar verification via TIM3 DMA
  *
  *          Same test logic as TEST_OV7670_COLORBAR but reads the AL422B FIFO
  *          via TIM3 DMA (RCK = TIM3 CH4 PWM) instead of GPIO bit-bang.
  *          Log output is identical so the same render script works.
  *
  *          @todo  DMA read data does not match GPIO bit-bang version.
  *                 Suspected cause: extra RCK pulses from TIM3 start/stop
  *                 per 32-byte chunk desync the FIFO read pointer.
  *                 Possible fix: TIM3 one-pulse mode with RCR for
  *                 hardware-exact pulse count per chunk.
  */
#ifndef TEST_OV7670_COLORBAR_DMA_H
#define TEST_OV7670_COLORBAR_DMA_H

/** @brief  Run all OV7670 colorbar DMA verification tests */
void RunOv7670ColorbarDmaTests(void);

#endif /* TEST_OV7670_COLORBAR_DMA_H */
