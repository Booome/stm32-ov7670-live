/**
  * @file    test_pipeline.c
  * @brief   TEST_PIPELINE group - OV7670 colorbar -> LCD live pipeline
  *
  *          Enables OV7670 colorbar test pattern as a deterministic video
  *          source and runs the full VSYNC->FIFO->Camera DMA->SPI DMA->LCD
  *          pipeline. Prints FPS once per second, deferred to the pipeline
  *          IDLE window so the blocking UART never delays frame read start.
  *
  *          No Unity dependency: pure visual + serial observation test.
  */
#include "test_pipeline.h"
#include "pipeline.h"
#include "ov7670.h"
#include "st7735.h"
#include "dwt_delay.h"
#include "debug.h"
#include "stm32f1xx_hal.h"
#include "main.h"

#define TEST_PIPELINE_FPS_WINDOW_MS  1000u

void RunPipelineTests(void)
{
  DWT_Init();

  if (!OV7670_Init())
  {
    debug_printf("[TEST_PIPELINE] OV7670_Init FAILED\n");
    Error_Handler();
  }
  debug_printf("[TEST_PIPELINE] OV7670 colorbar -> LCD live pipeline test\n");

  OV7670_EnableColorBar();
  debug_printf("[TEST_PIPELINE] colorbar enabled (COM3 bit0)\n");

  LCD_Init();
  debug_printf("[TEST_PIPELINE] LCD init OK\n");

  Pipeline_Init();
  Pipeline_EnableTimDma();
  Pipeline_ClearVsyncPending();
  Pipeline_EnableVsyncIrq();
  debug_printf("[TEST_PIPELINE] VSYNC IRQ enabled, streaming...\n");

  uint32_t last_frames = Pipeline_GetFrameCount();
  uint32_t last_tick   = HAL_GetTick();
  bool     print_pending = false;
  uint8_t  last_fps      = 0u;

  for (;;)
  {
    Pipeline_Poll();

    /* 1-second FPS window (SysTick based) */
    if (HAL_GetTick() - last_tick >= TEST_PIPELINE_FPS_WINDOW_MS)
    {
      uint32_t frames_now = Pipeline_GetFrameCount();
      last_fps     = (uint8_t)(frames_now - last_frames);
      last_frames  = frames_now;
      print_pending = true;
      last_tick    = HAL_GetTick();
    }

    /* Deferred print: only when pipeline is IDLE (no timing impact) */
    if (print_pending && Pipeline_GetState() == PIPELINE_STATE_IDLE)
    {
      debug_printf("[TEST_PIPELINE] fps=%u state=IDLE frames=%lu\n",
                   last_fps, (unsigned long)Pipeline_GetFrameCount());
      print_pending = false;
    }
  }
}
