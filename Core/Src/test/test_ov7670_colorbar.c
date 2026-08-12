/**
  * @file    test_ov7670_colorbar.c
  * @brief   TEST_OV7670_COLORBAR group - colorbar pixel data verification
  *
  *          Self-contained test group. Owns its own OV7670_Init + colorbar
  *          enable sequence, then bypasses the DMA pipeline and reads the
  *          AL422B FIFO directly via GPIO bit-banged RCK. Verifies vertical
  *          bar constancy (all rows identical), frozen-frame stability, and
  *          8 distinct segment values.
  */
#include "test_ov7670_colorbar.h"
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

/* ---- Dimension consistency ---- */

_Static_assert(PIPELINE_WIDTH == LCD_TEST_WIDTH,
               "PIPELINE_WIDTH must equal LCD_TEST_WIDTH (160)");
_Static_assert(PIPELINE_HEIGHT == LCD_TEST_HEIGHT,
               "PIPELINE_HEIGHT must equal LCD_TEST_HEIGHT (128)");
_Static_assert(PIPELINE_HALF_SIZE == LCD_TEST_LINE_SIZE,
               "PIPELINE_HALF_SIZE must equal LCD_TEST_LINE_SIZE (320B/row)");

#define CB_ROW_BYTES 320u
#define CB_ROWS      128u
#define CB_SEG_PX    20u    /* 160px / 8 bars */
#define CB_SEG_BYTES 40u   /* 20px * 2B */

static uint8_t s_line_buf[CB_ROW_BYTES * 2u];  /* row_ref + row_cur */

/* ---- Low-level FIFO access ---- */

static inline void OV7670_FIFO_RCK_Low(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_RCK_GPIO_Port, OV7670_FIFO_RCK_Pin, GPIO_PIN_RESET);
}

static inline void OV7670_FIFO_RCK_High(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_RCK_GPIO_Port, OV7670_FIFO_RCK_Pin, GPIO_PIN_SET);
}

static void FifoReadMode_Enter(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = OV7670_FIFO_RCK_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(OV7670_FIFO_RCK_GPIO_Port, &gpio);
  OV7670_FIFO_RCK_Low();
}

static void FifoReadMode_Exit(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = OV7670_FIFO_RCK_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(OV7670_FIFO_RCK_GPIO_Port, &gpio);
}

static void ResetReadPointer(void)
{
  OV7670_FIFO_RRST_Low();
  OV7670_FIFO_RCK_High();
  OV7670_FIFO_RCK_Low();
  OV7670_FIFO_RRST_High();
}

static uint8_t ReadFifoByte(void)
{
  OV7670_FIFO_RCK_High();
  DWT_DelayCycles(4u);
  uint8_t data = (uint8_t)(GPIOA->IDR & 0xFFu);
  OV7670_FIFO_RCK_Low();
  DWT_DelayCycles(2u);
  return data;
}

static void ReadFifoLineLen(uint8_t *line, uint16_t len)
{
  for (uint16_t i = 0u; i < len; i++)
  {
    line[i] = ReadFifoByte();
  }
}

static uint16_t ReadWordFromBuf(const uint8_t *buf, uint16_t offset)
{
  return (uint16_t)(buf[offset] | (uint16_t)(buf[offset + 1u] << 8u));
}

/* ---- VSYNC edge detection ---- */

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

/* ---- Tests ---- */

static void TestColorbarInit(void)
{
  TEST_ASSERT_TRUE(OV7670_Init());
}

/**
  * @brief  Enable sensor colorbar, capture one VSYNC-gated frame,
  *         then verify vertical-bar constancy, frozen-frame stability,
  *         and 8 distinct segment mean values.
  */
static void TestColorbarFifoData(void)
{
  uint8_t *row_ref = s_line_buf;
  uint8_t *row_cur = s_line_buf + CB_ROW_BYTES;
  uint32_t seg_sum[8];
  uint16_t seg_mean[8];
  uint16_t identical_rows;

  /* Runtime dimension check */
  TEST_ASSERT_EQUAL_UINT16(LCD_TEST_WIDTH, PIPELINE_WIDTH);
  TEST_ASSERT_EQUAL_UINT16(LCD_TEST_HEIGHT, PIPELINE_HEIGHT);
  TEST_ASSERT_EQUAL_UINT16(LCD_TEST_LINE_SIZE, PIPELINE_HALF_SIZE);

  OV7670_Init();

  /* Enable sensor colorbar (COM7 bit1) with confirmed scale chain.
   * YSC7=1 selects 8-bar test pattern (XSC7=0, YSC7=1 per empirical data). */
  SCCB_WriteReg(0x12u, 0x06u);              /* COM7: VGA+RGB565+sensor colorbar */
  SCCB_WriteReg(0x42u, 0x00u);              /* COM17: no DSP colorbar */
  SCCB_WriteReg(0x70u, 0x40u);              /* XSC: XSC7=0, scale=64 */
  SCCB_WriteReg(0x71u, 0x3Cu | 0x80u);      /* YSC: YSC7=1, scale=60 */
  SCCB_WriteReg(0x13u, 0x80u);              /* COM8: disable AWB+AGC+AEC */

  DWT_DelayMs(1000u);

  /* VSYNC-aligned one-frame capture */
  TEST_ASSERT_TRUE_MESSAGE(WaitVsyncEdge(true), "VSYNC timeout (1st)");

  OV7670_FIFO_WR_Low();
  OV7670_FIFO_WRST_Low();
  DWT_DelayCycles(10u);
  OV7670_FIFO_WRST_High();
  OV7670_FIFO_WR_High();

  TEST_ASSERT_TRUE_MESSAGE(WaitVsyncEdge(true), "VSYNC timeout (2nd)");
  OV7670_FIFO_WR_Low();

  /* Read all rows */
  OV7670_FIFO_OE_Low();
  ResetReadPointer();
  ReadFifoLineLen(row_ref, CB_ROW_BYTES);

  identical_rows = 0u;
  uint8_t first_diff = 0xFFu;
  for (uint16_t y = 1u; y < CB_ROWS; y++)
  {
    ReadFifoLineLen(row_cur, CB_ROW_BYTES);
    if (memcmp(row_ref, row_cur, CB_ROW_BYTES) == 0)
    {
      identical_rows++;
    }
    else if (first_diff == 0xFFu)
    {
      first_diff = (uint8_t)y;
    }
  }

  /* Frozen check: re-read row 0 after 100ms */
  OV7670_FIFO_OE_High();
  DWT_DelayMs(100u);
  OV7670_FIFO_OE_Low();
  ResetReadPointer();
  ReadFifoLineLen(row_cur, CB_ROW_BYTES);
  bool frozen = (memcmp(row_ref, row_cur, CB_ROW_BYTES) == 0);

  OV7670_FIFO_OE_High();
  DWT_DelayMs(20u);

  if (first_diff == 0xFFu)
  {
    debug_printf("  [cb] rows_identical=%u/%u first_diff=none frozen=%s\n",
                 (unsigned)identical_rows, (unsigned)(CB_ROWS - 1u),
                 frozen ? "yes" : "no");
  }
  else
  {
    debug_printf("  [cb] rows_identical=%u/%u first_diff=%u frozen=%s\n",
                 (unsigned)identical_rows, (unsigned)(CB_ROWS - 1u),
                 (unsigned)first_diff, frozen ? "yes" : "no");
  }

  /* 8-segment mean check */
  debug_printf("  [cb] segs:");
  for (uint16_t s = 0u; s < 8u; s++)
  {
    seg_sum[s] = 0u;
    for (uint16_t i = 0u; i < CB_SEG_PX; i++)
    {
      seg_sum[s] += ReadWordFromBuf(row_ref, (uint32_t)s * CB_SEG_BYTES + (uint32_t)i * 2u);
    }
    seg_mean[s] = (uint16_t)(seg_sum[s] / CB_SEG_PX);
    debug_printf(" s%u=0x%04X", (unsigned)s, (unsigned)seg_mean[s]);
  }
  debug_printf("\n");

  /* Distinctness check: all 8 segment means must differ */
  bool distinct = true;
  for (uint16_t s = 1u; s < 8u; s++)
  {
    for (uint16_t k = 0u; k < s; k++)
    {
      if (seg_mean[s] == seg_mean[k])
      {
        distinct = false;
        break;
      }
    }
    if (!distinct) break;
  }

  /* Full-frame hex dump for host-side chart rendering */
  {
    OV7670_FIFO_OE_High();
    DWT_DelayMs(10u);
    OV7670_FIFO_OE_Low();
    ResetReadPointer();
    debug_printf("  [cb] FRAME_START\n");
    uint32_t total = (uint32_t)CB_ROWS * CB_ROW_BYTES;
    for (uint32_t i = 0u; i < total; i++)
    {
      uint8_t b = ReadFifoByte();
      if (i % 32u == 0u)
      {
        debug_printf("    %08x: ", (unsigned)i);
      }
      debug_printf("%02x ", (unsigned)b);
      if ((i + 1u) % 32u == 0u)
      {
        debug_printf("\n");
      }
    }
    if (total % 32u != 0u)
    {
      debug_printf("\n");
    }
    debug_printf("  [cb] FRAME_END\n");
  }

  TEST_ASSERT_TRUE_MESSAGE(identical_rows >= CB_ROWS - 8u,
                           "too many rows differ from row0 (sensor artifact)");
  TEST_ASSERT_TRUE_MESSAGE(frozen, "row0 not frozen");
  TEST_ASSERT_TRUE_MESSAGE(distinct, "segments not all distinct");
}

void RunOv7670ColorbarTests(void)
{
  UNITY_BEGIN();

  DWT_Init();
  SCCB_Init();
  DWT_DelayMs(10u);

  FifoReadMode_Enter();

  debug_printf("[TEST_OV7670_COLORBAR] OV7670 colorbar FIFO data verification\n");

  RUN_TEST(TestColorbarInit);
  DWT_DelayMs(10u);
  RUN_TEST(TestColorbarFifoData);

  FifoReadMode_Exit();

  UNITY_END();
}
