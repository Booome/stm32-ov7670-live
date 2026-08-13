/**
  * @file    test_pipeline.c
  * @brief   TEST_PIPELINE group - OV7670 colorbar -> LCD live pipeline
  *
  *          Enables the OV7670 test pattern as a deterministic video source
  *          and runs the full VSYNC->FIFO->Camera DMA->SPI DMA->LCD pipeline.
  *          Prints FPS once per second, deferred to the pipeline IDLE window
  *          so the blocking UART never delays frame read start.
  *
  *          Known facts (verified in TEST_OV7670_COLORBAR):
  *          - Sensor scaling 160x128 is effective: frame = 40960B < 384KB FIFO.
  *          - Sensor colorbar (COM7 bit1) produces the standard 8 vertical bars.
  *          - QVGA (150KB/frame) cannot fit the FIFO - never use QVGA mode.
 *          - Avg write 1.23MB/s < read 1.44MB/s; read starts 15ms after
 *            VSYNC (write lead ~18.5KB) so the read never overtakes.
 *          - Read spans [15, 43.4]ms and overlaps the next frame's write
 *            (next VSYNC at 34.8ms): the next write overwrites the FIFO front
 *            half the reader already passed -> ~28.7 fps.
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
#define TEST_PIPELINE_FIFO_BYTES     393216u             /* AL422B 3Mbit */

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
  debug_printf("[TEST_PIPELINE] colorbar enabled (8-bar)\n");

  debug_printf("[TEST_PIPELINE] res=160x128 frame=%uB fifo=%uB (fits)\n",
               PIPELINE_FRAME_SIZE, TEST_PIPELINE_FIFO_BYTES);

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
      debug_printf("[TEST_PIPELINE] fps=%u (max~28.7) state=IDLE frames=%lu\n",
                   last_fps, (unsigned long)Pipeline_GetFrameCount());
      print_pending = false;
    }
  }
}
