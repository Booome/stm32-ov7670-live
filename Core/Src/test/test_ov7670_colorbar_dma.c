/**
  * @file    test_ov7670_colorbar_dma.c
  * @brief   TEST_OV7670_COLORBAR_DMA group - colorbar verification via TIM3 DMA
  *
  *          Same test logic as TEST_OV7670_COLORBAR but reads the AL422B FIFO
  *          via TIM3 DMA (RCK = TIM3 CH4 PWM at 1.44 MHz) instead of GPIO
  *          bit-bang.  Log output is byte-identical so the same render script
  *          (render_colorbar_charts.py) works without modification.
  *
  *          Resolution: QQVGA 160x120 RGB565.
  */
#include "test_ov7670_colorbar_dma.h"
#include "test_lcd_common.h"
#include "pipeline.h"
#include "ov7670.h"
#include "ov7670_sccb.h"
#include "dwt_delay.h"
#include "unity.h"
#include "debug.h"
#include "stm32f1xx_hal.h"
#include "main.h"
#include <string.h>

/* External handles (defined in main.c) */
extern TIM_HandleTypeDef htim3;
extern DMA_HandleTypeDef hdma_tim3_ch4_up;

_Static_assert(PIPELINE_WIDTH == LCD_TEST_WIDTH,
              "PIPELINE_WIDTH must equal LCD_TEST_WIDTH (160)");
_Static_assert(PIPELINE_HEIGHT == LCD_TEST_HEIGHT,
              "PIPELINE_HEIGHT must equal LCD_TEST_HEIGHT (120)");
_Static_assert(PIPELINE_HALF_SIZE == LCD_TEST_LINE_SIZE,
              "PIPELINE_HALF_SIZE must equal LCD_TEST_LINE_SIZE (320B/row)");

/* ---- RCK pin mode switching (shared with GPIO bit-bang tests) ---- */

static void FifoRck_EnterGpio(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = OV7670_FIFO_RCK_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(OV7670_FIFO_RCK_GPIO_Port, &gpio);
  HAL_GPIO_WritePin(OV7670_FIFO_RCK_GPIO_Port, OV7670_FIFO_RCK_Pin,
                    GPIO_PIN_RESET);
}

static void FifoRck_EnterAf(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = OV7670_FIFO_RCK_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(OV7670_FIFO_RCK_GPIO_Port, &gpio);
}

/**
  * @brief  Reset AL422B read pointer: RRST low + one RCK falling edge.
  *
  *         Temporarily switches RCK to GPIO to generate the pulse, then
  *         restores RCK to AF (TIM3 CH4) for DMA operation.
  */
static void ResetReadPointer(void)
{
  FifoRck_EnterGpio();
  OV7670_FIFO_RRST_Low();
  HAL_GPIO_WritePin(OV7670_FIFO_RCK_GPIO_Port, OV7670_FIFO_RCK_Pin,
                    GPIO_PIN_SET);
  HAL_GPIO_WritePin(OV7670_FIFO_RCK_GPIO_Port, OV7670_FIFO_RCK_Pin,
                    GPIO_PIN_RESET);
  OV7670_FIFO_RRST_High();
  FifoRck_EnterAf();
}

/**
  * @brief  Read one chunk of bytes from FIFO via TIM3 DMA.
  *
  *         Starts TIM3 CH4 PWM (RCK clock), DMA reads GPIOA->IDR into buf,
  *         waits for DMA complete, then stops TIM3.
  *
  * @param  buf   Destination buffer (must be >= len bytes)
  * @param  len   Number of bytes to read (must be > 0)
  */
static void DmaReadChunk(uint8_t *buf, uint16_t len)
{
  /* Set DMA to normal mode (not circular) for single-chunk transfer */
  hdma_tim3_ch4_up.Init.Mode = DMA_NORMAL;
  HAL_DMA_Init(&hdma_tim3_ch4_up);

  /* Enable TIM3 CC4 DMA request */
  __HAL_TIM_ENABLE_DMA(&htim3, TIM_DMA_CC4);

  /* Start DMA: GPIOA->IDR -> buf, normal mode, len bytes */
  HAL_DMA_Start(&hdma_tim3_ch4_up,
                (uint32_t)OV7670_DATA_ADDR,
                (uint32_t)buf,
                (uint32_t)len);

  /* Start TIM3 CH4 PWM (generates RCK at 1.44 MHz) */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

  /* Wait for DMA transfer to complete */
  HAL_DMA_PollForTransfer(&hdma_tim3_ch4_up, HAL_DMA_FULL_TRANSFER,
                          HAL_MAX_DELAY);

  /* Stop TIM3 */
  HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_4);

  /* Restore DMA to circular mode for pipeline compatibility */
  hdma_tim3_ch4_up.Init.Mode = DMA_CIRCULAR;
  HAL_DMA_Init(&hdma_tim3_ch4_up);
}

/* ---- VSYNC edge detection (same as GPIO version) ---- */

static bool WaitVsyncEdge(bool rising)
{
  bool prev = (GPIOA->IDR & OV7670_VSYNC_Pin) != 0;
  for (uint32_t t = 0u; t < 10000000u; t++)
  {
    bool cur = (GPIOA->IDR & OV7670_VSYNC_Pin) != 0;
    if ((rising && cur && !prev) || (!rising && !cur && prev))
    {
      return true;
    }
    prev = cur;
  }
  return false;
}

/* ---- Test: OV7670 init must succeed ---- */

static void TestColorbarDmaInit(void)
{
  TEST_ASSERT_TRUE(OV7670_Init());
}

/* ---- Full-frame 8-bar verification via DMA ---- */

#define VF_ROW_BYTES 320u   /* 160px * 2B */
#define VF_ROWS      120u

/**
  * @brief  Full-frame colorbar verification using TIM3 DMA FIFO read.
  *
  *         Same logic as GPIO version: capture one frame to FIFO, then
  *         read back via DMA and verify row structure + dump hex stream.
  */
static bool TrialVsyncFrameFull(uint8_t xsc, uint8_t ysc, const char *label)
{
  bool verdict = false;
  OV7670_Init();

  /* Enable sensor colorbar */
  SCCB_WriteReg(0x12u, 0x06u);  /* COM7: RGB565 + sensor colorbar */
  SCCB_WriteReg(0x42u, 0x00u);  /* COM17: no DSP colorbar */
  SCCB_WriteReg(0x70u, xsc);
  SCCB_WriteReg(0x71u, ysc);
  SCCB_WriteReg(0x13u, 0x80u);  /* COM8: disable AWB+AGC+AEC */

  /* Readback diagnostics */
  debug_printf("  [%s] readback COM7=0x%02X COM8=0x%02X COM17=0x%02X XSC=0x%02X YSC=0x%02X\n",
               label, SCCB_ReadReg(0x12u), SCCB_ReadReg(0x13u),
               SCCB_ReadReg(0x42u),
               SCCB_ReadReg(0x70u), SCCB_ReadReg(0x71u));
  debug_printf("  [%s] fmt: COM1=0x%02X COM3=0x%02X TSLB=0x%02X COM13=0x%02X COM14=0x%02X COM15=0x%02X RGB444=0x%02X\n",
               label, SCCB_ReadReg(0x04u), SCCB_ReadReg(0x0Cu), SCCB_ReadReg(0x3Au),
               SCCB_ReadReg(0x3Du), SCCB_ReadReg(0x3Eu), SCCB_ReadReg(0x40u),
               SCCB_ReadReg(0x8Cu));
  debug_printf("  [%s] color: COM16=0x%02X RED=0x%02X BLUE=0x%02X GGAIN=0x%02X\n",
               label, SCCB_ReadReg(0x41u), SCCB_ReadReg(0xD0u),
               SCCB_ReadReg(0xCFu), SCCB_ReadReg(0xCEu));

  DWT_DelayMs(1000u);

  /* Wait for VSYNC rising edge */
  if (!WaitVsyncEdge(true))
  {
    debug_printf("  [%s] VSYNC timeout (1st)\n", label);
    return false;
  }

  /* Capture one frame to FIFO */
  OV7670_FIFO_WR_Low();
  OV7670_FIFO_WRST_Low();
  DWT_DelayCycles(10u);
  OV7670_FIFO_WRST_High();

  uint32_t w_start = DWT_GetCycles();
  OV7670_FIFO_WR_High();

  uint32_t idr0 = GPIOA->IDR;
  uint32_t idr1 = GPIOB->IDR;
  bool vsync_at_wr = (idr0 & OV7670_VSYNC_Pin) != 0;
  bool href_at_wr  = (idr0 & OV7670_HREF_Pin)  != 0;
  bool wr_at_wr    = (idr1 & OV7670_FIFO_WR_Pin) != 0;

  /* Wait for NEXT VSYNC rising edge to close write window */
  if (!WaitVsyncEdge(true))
  {
    debug_printf("  [%s] VSYNC timeout (2nd)\n", label);
    OV7670_FIFO_WR_Low();
    return false;
  }
  OV7670_FIFO_WR_Low();
  uint32_t w_end = DWT_GetCycles();

  debug_printf("  [%s] wr_high: VSYNC=%u HREF=%u WR_PB5=%u\n",
               label, (unsigned)vsync_at_wr, (unsigned)href_at_wr,
               (unsigned)wr_at_wr);
  debug_printf("  [%s] vsync_write_window_ms=%lu\n",
               label, (unsigned long)((w_end - w_start) / 72000u));

  /* ---- Phase C: row-width check via DMA ---- */
  static uint8_t s_line_buf_a[VF_ROW_BYTES];
  static uint8_t s_line_buf_b[VF_ROW_BYTES];
  uint8_t *line_ref = s_line_buf_a;
  uint8_t *line_cur = s_line_buf_b;

  OV7670_FIFO_OE_High();
  DWT_DelayMs(10u);
  OV7670_FIFO_OE_Low();
  ResetReadPointer();

  /* Read row 0 via DMA (10 chunks of 32 bytes) */
  for (uint16_t c = 0u; c < VF_ROW_BYTES / 32u; c++)
  {
    DmaReadChunk(&line_ref[c * 32u], 32u);
  }

  uint16_t identical_rows = 0u;
  uint8_t first_diff = 0xFFu;
  for (uint16_t y = 1u; y < VF_ROWS; y++)
  {
    for (uint16_t c = 0u; c < VF_ROW_BYTES / 32u; c++)
    {
      DmaReadChunk(&line_cur[c * 32u], 32u);
    }
    if (memcmp(line_ref, line_cur, VF_ROW_BYTES) == 0)
    {
      identical_rows++;
    }
    else if (first_diff == 0xFFu)
    {
      first_diff = (uint8_t)y;
    }
  }

  /* Re-read row 0 after 100ms to prove frozen */
  OV7670_FIFO_OE_High();
  DWT_DelayMs(100u);
  OV7670_FIFO_OE_Low();
  ResetReadPointer();
  for (uint16_t c = 0u; c < VF_ROW_BYTES / 32u; c++)
  {
    DmaReadChunk(&line_cur[c * 32u], 32u);
  }
  bool frozen_row0 = (memcmp(line_ref, line_cur, VF_ROW_BYTES) == 0);

  OV7670_FIFO_OE_High();
  DWT_DelayMs(20u);

  /* Row-matching report */
  if (first_diff == 0xFFu)
  {
    debug_printf("  [%s] rows_identical=%u/%u first_diff_row=none frozen_row0=%s\n",
                 label, (unsigned)identical_rows, (unsigned)(VF_ROWS - 1u),
                 frozen_row0 ? "yes" : "no");
  }
  else
  {
    debug_printf("  [%s] rows_identical=%u/%u first_diff_row=%u frozen_row0=%s\n",
                 label, (unsigned)identical_rows, (unsigned)(VF_ROWS - 1u),
                 (unsigned)first_diff, frozen_row0 ? "yes" : "no");
  }

  /* Row-0 8-bar structure */
  debug_printf("  [%s] row0 segs:", label);
  bool seg_all_uniform = true;
  uint16_t seg_val[8] = {0u};
  for (uint16_t s = 0u; s < 8u; s++)
  {
    uint16_t seg0 = (uint16_t)(line_ref[s * 40u] |
                    (uint16_t)(line_ref[s * 40u + 1u] << 8u));
    seg_val[s] = seg0;
    bool seg_uniform = true;
    for (uint16_t i = 1u; i < 20u; i++)
    {
      uint16_t px = (uint16_t)(line_ref[s * 40u + i * 2u] |
                     (uint16_t)(line_ref[s * 40u + i * 2u + 1u] << 8u));
      if (px != seg0)
      {
        seg_uniform = false;
        break;
      }
    }
    seg_all_uniform &= seg_uniform;
    debug_printf(" s%u=%s(0x%04X)", (unsigned)s,
                 seg_uniform ? "u" : "MIXED", (unsigned)seg0);
  }
  debug_printf("\n");
  debug_printf("  [%s] row0 all8seg_uniform=%s\n",
               label, seg_all_uniform ? "yes" : "no");

  /* Verdict */
  verdict = (identical_rows == VF_ROWS - 1u) &&
            frozen_row0 && seg_all_uniform;
  for (uint16_t s = 1u; s < 8u && verdict; s++)
  {
    for (uint16_t k = 0u; k < s; k++)
    {
      if (seg_val[s] == seg_val[k])
      {
        verdict = false;
        break;
      }
    }
  }
  debug_printf("  [%s] => %s\n", label, verdict ? "REAL 8-BAR" : "NOT 8-bar");

  /* ---- Phase D: full-frame hexdump via DMA ---- */
  {
    OV7670_FIFO_OE_High();
    DWT_DelayMs(10u);
    OV7670_FIFO_OE_Low();
    ResetReadPointer();
    debug_printf("  [%s] FRAME_START\n", label);
    uint32_t total = (uint32_t)VF_ROWS * VF_ROW_BYTES;
    uint16_t cnt[256] = {0u};
    uint16_t distinct = 0u;
    uint8_t chunk[32u];
    for (uint32_t offset = 0u; offset < total; offset += 32u)
    {
      DmaReadChunk(chunk, 32u);
      debug_printf("    %08x: ", (unsigned)offset);
      for (uint16_t j = 0u; j < 32u; j++)
      {
        uint8_t b = chunk[j];
        if (cnt[b] == 0u)
        {
          distinct++;
        }
        cnt[b]++;
        debug_printf("%02x ", (unsigned)b);
      }
      debug_printf("\n");
    }
    debug_printf("  [%s] FRAME_END\n", label);

    /* Histogram summary */
    uint16_t top[4] = {0u, 0u, 0u, 0u};
    uint16_t topv[4] = {0u, 0u, 0u, 0u};
    for (uint16_t v = 0u; v < 256u; v++)
    {
      if (cnt[v] == 0u)
      {
        continue;
      }
      uint16_t rank = 4u;
      while (rank > 0u && cnt[v] > top[rank - 1u])
      {
        if (rank < 4u)
        {
          top[rank] = top[rank - 1u];
          topv[rank] = topv[rank - 1u];
        }
        rank--;
      }
      if (rank < 4u)
      {
        top[rank] = cnt[v];
        topv[rank] = v;
      }
    }
    debug_printf("  [%s] distinct_bytes=%u", label, (unsigned)distinct);
    for (uint16_t k = 0u; k < 4u; k++)
    {
      debug_printf(" [0x%02X]=%u", (unsigned)topv[k], (unsigned)top[k]);
    }
    debug_printf("\n");
  }

  OV7670_Init();
  DWT_DelayMs(200u);
  return verdict;
}

/* ---- Test entry ---- */

static void TestColorbarDmaFifoData(void)
{
  const struct
  {
    uint8_t xsc;
    uint8_t ysc;
    const char *label;
  } dirs[] =
  {
    { 0x3Au,         0x35u,       "tp=00 XSC7=0 YSC7=0" },
    { 0x3Au | 0x80u, 0x35u,       "tp=01 XSC7=1 YSC7=0" },
    { 0x3Au,         0x35u | 0x80u, "tp=10 XSC7=0 YSC7=1" },
    { 0x3Au | 0x80u, 0x35u | 0x80u, "tp=11 XSC7=1 YSC7=1" },
  };

  bool any_8bar = false;
  for (uint16_t i = 0u; i < sizeof(dirs) / sizeof(dirs[0]); i++)
  {
    any_8bar |= TrialVsyncFrameFull(dirs[i].xsc, dirs[i].ysc, dirs[i].label);
  }
  TEST_ASSERT_TRUE_MESSAGE(any_8bar, "neither test_pattern direction produced a real 8-bar");
}

void RunOv7670ColorbarDmaTests(void)
{
  UNITY_BEGIN();

  DWT_Init();
  SCCB_Init();
  DWT_DelayMs(10u);

  /* RCK stays in AF mode (TIM3 CH4) for DMA -- no GPIO switch needed */

  debug_printf("[TEST_OV7670_COLORBAR_DMA] OV7670 colorbar TIM3 DMA verification\n");

  RUN_TEST(TestColorbarDmaInit);
  DWT_DelayMs(10u);

  RUN_TEST(TestColorbarDmaFifoData);

  UNITY_END();
}
