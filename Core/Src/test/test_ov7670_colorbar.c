/**
  * @file    test_ov7670_colorbar.c
  * @brief   TEST_OV7670_COLORBAR group - colorbar pixel data verification
  *
  *          Self-contained test group. Owns its own OV7670_Init + 8-bar
  *          enable sequence (no dependence on TEST_OV7670 group state), then
  *          bypasses the DMA pipeline and reads the AL422B FIFO directly via
  *          GPIO bit-banged RCK. Each 320B row is verified for vertical-bar
  *          constancy (all rows identical) and frozen-frame stability.
  *
 *          This separates "colorbar configuration wrong" from
 *          "pipeline/LCD timing broken".
 *
 *          Resolution: 160x128 RGB565 (OV7670 QVGA 320x240 -> DCW ->
 *          XSC/YSC digital zoom, as configured by OV7670_Init()).
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

/* ---- Dimension consistency: FIFO line layout must match LCD ---- */

_Static_assert(PIPELINE_WIDTH == LCD_TEST_WIDTH,
              "PIPELINE_WIDTH must equal LCD_TEST_WIDTH (160)");
_Static_assert(PIPELINE_HEIGHT == LCD_TEST_HEIGHT,
              "PIPELINE_HEIGHT must equal LCD_TEST_HEIGHT (128)");
_Static_assert(PIPELINE_HALF_SIZE == LCD_TEST_LINE_SIZE,
              "PIPELINE_HALF_SIZE must equal LCD_TEST_LINE_SIZE (320B/row)");

/* Shared scratch buffer reused by the sequential experiments (RAM budget) */
#define SHARE_BUF_SIZE 8192u
static uint8_t s_share_buf[SHARE_BUF_SIZE];

/* ---- Low-level FIFO access ---- */

/** @brief  Drive RCK pin low (assert read clock) */
static inline void OV7670_FIFO_RCK_Low(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_RCK_GPIO_Port, OV7670_FIFO_RCK_Pin, GPIO_PIN_RESET);
}

/** @brief  Drive RCK pin high (release read clock) */
static inline void OV7670_FIFO_RCK_High(void)
{
  HAL_GPIO_WritePin(OV7670_FIFO_RCK_GPIO_Port, OV7670_FIFO_RCK_Pin, GPIO_PIN_SET);
}

/**
  * @brief  Configure RCK pin as GPIO output for bit-banged FIFO read.
  *         Call once before any ReadFifoByte / ResetReadPointer.
  */
static void FifoReadMode_Enter(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = OV7670_FIFO_RCK_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(OV7670_FIFO_RCK_GPIO_Port, &gpio);
  OV7670_FIFO_RCK_Low();
}

/**
  * @brief  Restore RCK pin to alternate function (DMA pipeline mode).
  */
static void FifoReadMode_Exit(void)
{
  GPIO_InitTypeDef gpio = {0};
  gpio.Pin = OV7670_FIFO_RCK_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(OV7670_FIFO_RCK_GPIO_Port, &gpio);
}

/**
  * @brief  AL422B read-pointer reset (per C8051 reference main.c):
  *         RRST low + at least one RCK falling edge, then RRST high.
  *         Without an RCK pulse while RRST is low the read address never
  *         returns to 0, so the FIFO data is read from a drifting address.
  */
static void ResetReadPointer(void)
{
  OV7670_FIFO_RRST_Low();
  OV7670_FIFO_RCK_High();
  OV7670_FIFO_RCK_Low();
  OV7670_FIFO_RRST_High();
}

/**
  * @brief  Read one byte per AL422B datasheet: the read address advances and
  *         the data becomes valid after T_AC on the RCK rising edge.
  *         Drive RCK high (rising edge), wait T_AC, sample D0-D7, then
  *         RCK low.  RCK starts from low, so every call generates a full
  *         clock cycle including the very first byte.
  */
static uint8_t ReadFifoByte(void)
{
  OV7670_FIFO_RCK_High();
  DWT_DelayCycles(4u);   /* wait T_AC (15ns) for data to become valid */
  uint8_t data = (uint8_t)(GPIOA->IDR & 0xFFu);   /* D0-D7 = PA0-PA7 */
  OV7670_FIFO_RCK_Low();
  DWT_DelayCycles(2u);
  return data;
}

/** @brief  Read a line of len bytes */
static void ReadFifoLineLen(uint8_t *line, uint16_t len)
{
  for (uint16_t i = 0u; i < len; i++)
  {
    line[i] = ReadFifoByte();
  }
}

/* ---- Test: OV7670 init must succeed (self-contained) ---- */

static void TestColorbarInit(void)
{
  TEST_ASSERT_TRUE(OV7670_Init());
}

/* ---- VSYNC edge detection ---- */

/** @brief  Wait for a VSYNC edge, returns false on timeout
  * @param  rising  true = rising edge, false = falling edge
  * @retval true if edge observed within timeout, false otherwise
  */
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

/* ---- Full-frame 8-bar verification ---- */

/**
  * @brief  Full-frame row verification at 160x120 for one test_pattern
  *         bit direction.
  *
  *         COM7 bit1 + COM17 bit3 enable the colorbar; the two possible
  *         test_pattern bit directions (SCALING_XSC[7]/SCALING_YSC[7]) are
  *         passed in, since the datasheet table is ambiguous.
  *
  *         The whole 38400B frame is read row-by-row (320B/row) without
  *         resetting the read pointer, so rows are contiguous. Vertical
  *         color bars imply every row equals row 0; any mismatch flags
  *         inter-row padding or a FIFO write gap. Row 0 is re-read after
  *         100ms to prove the whole frame is frozen.
  *
  *         Returns true only if a real 8-bar is observed: all rows equal,
  *         frozen, all 8 segments internally uniform, and the 8 segment
  *         values mutually distinct (excludes all-black/all-white).
  *
  * @param  xsc    SCALING_XSC value (bit7 = test_pattern[0])
  * @param  ysc    SCALING_YSC value (bit7 = test_pattern[1])
  * @param  label  Direction label for diagnostics
  * @retval true if a real 8-bar pattern was observed
  */
static bool TrialVsyncFrameFull(uint8_t xsc, uint8_t ysc, const char *label)
{
  bool verdict = false;
  OV7670_Init();   /* our 160x128 scale: VGA+DCW by2+XSC/YSC */

  /* Enable sensor colorbar only, keeping OV7670_Init()'s VGA 160x128
   * scale chain intact (COM3=0x0C DCW+DZ, COM14=0x1A manual scale,
   * DCWCTR=0x11 by2, scale values in XSC/YSC below). COM7 stays
   * VGA+RGB565 (0x04); bit1 adds the color bar. */
  SCCB_WriteReg(0x12u, 0x06u);  /* COM7: VGA + RGB565 + sensor colorbar */
  SCCB_WriteReg(0x42u, 0x00u);  /* COM17: no DSP colorbar */
  SCCB_WriteReg(0x70u, xsc);    /* SCALING_XSC (bit7 = test_pattern[0]) */
  SCCB_WriteReg(0x71u, ysc);    /* SCALING_YSC (bit7 = test_pattern[1]) */

  /* Disable AWB/AGC/AEC so the colorbar is not "white-balanced" by the
   * auto algorithm. COM8=0x80 keeps only FASTAEC+AECSTEP+BFILT.
   * NOTE: COM8 is at 0x13 (0x0E is COM5, a common mixup).
   * AWB gains, contrast, gamma, ABLC are already set by OV7670_Init(). */
  SCCB_WriteReg(0x13u, 0x80u);  /* COM8: disable AWB+AGC+AEC */

  /* Readback diagnostics: confirm writes actually landed */
  debug_printf("  [%s] readback COM7=0x%02X COM8=0x%02X COM17=0x%02X XSC=0x%02X YSC=0x%02X\n",
               label, SCCB_ReadReg(0x12u), SCCB_ReadReg(0x13u),
               SCCB_ReadReg(0x42u),
               SCCB_ReadReg(0x70u), SCCB_ReadReg(0x71u));
  debug_printf("  [%s] fmt: COM1=0x%02X COM3=0x%02X TSLB=0x%02X COM13=0x%02X COM14=0x%02X COM15=0x%02X RGB444=0x%02X\n",
               label, SCCB_ReadReg(0x04u), SCCB_ReadReg(0x0Cu), SCCB_ReadReg(0x3Au),
               SCCB_ReadReg(0x3Du), SCCB_ReadReg(0x3Eu), SCCB_ReadReg(0x40u),
               SCCB_ReadReg(0x8Cu));
  debug_printf("  [%s] color: COM16=0x%02X RED=0x%02X BLUE=0x%02X GGAIN=0x%02X\n",
               label, SCCB_ReadReg(0x41u), SCCB_ReadReg(0x02u),
               SCCB_ReadReg(0x01u), SCCB_ReadReg(0x6Au));

  DWT_DelayMs(1000u);   /* LONGER settle: OV7670_Init hard-resets the
                         * sensor, which needs >300ms for HREF/PCLK. */

  /* Wait for VSYNC rising edge (VBLANK start) to align write window */
  if (!WaitVsyncEdge(true))
  {
    debug_printf("  [%s] VSYNC timeout (1st)\n", label);
    return false;
  }

  OV7670_FIFO_WR_Low();
  OV7670_FIFO_WRST_Low();
  DWT_DelayCycles(10u);
  OV7670_FIFO_WRST_High();

  uint32_t w_start = DWT_GetCycles();
  OV7670_FIFO_WR_High();

  /* Snapshot VSYNC/HREF/WR at the instant WR goes high.
   * Do NOT printf here - UART is blocking and would delay
   * past the next VSYNC edge. Print after WR_Low instead. */
  uint32_t idr0 = GPIOA->IDR;
  uint32_t idr1 = GPIOB->IDR;
  bool vsync_at_wr = (idr0 & OV7670_VSYNC_Pin) != 0;
  bool href_at_wr  = (idr0 & OV7670_HREF_Pin)  != 0;
  bool wr_at_wr    = (idr1 & OV7670_FIFO_WR_Pin) != 0;

  /* Wait for NEXT VSYNC rising edge to close write window (= one frame) */
  if (!WaitVsyncEdge(true))
  {
    debug_printf("  [%s] VSYNC timeout (2nd)\n", label);
    OV7670_FIFO_WR_Low();
    return false;
  }
  OV7670_FIFO_WR_Low();
  uint32_t w_end = DWT_GetCycles();

  /* Now safe to print - write window is closed */
  debug_printf("  [%s] wr_high: VSYNC=%u HREF=%u WR_PB5=%u\n",
               label, (unsigned)vsync_at_wr, (unsigned)href_at_wr,
               (unsigned)wr_at_wr);
  debug_printf("  [%s] vsync_write_window_ms=%lu\n",
               label, (unsigned long)((w_end - w_start) / 72000u));

  /* Do NOT reset the sensor here: /WE = NAND(HREF, FIFO_WR) already gates
   * writes, so WR_Low froze the frame. Resetting OV7670 stops PCLK (=WCK),
   * which is the AL422B DRAM refresh clock - losing it corrupts FIFO data. */
#define VF_ROW_BYTES 320u   /* 160px * 2B */
#define VF_ROWS      128u
  uint8_t *line_ref = s_share_buf;                /* row 0 reference */
  uint8_t *line_cur = s_share_buf + VF_ROW_BYTES; /* current row */

  /* ---- Phase C: true row-width hypothesis check ----
   * If the sensor really outputs 160x120 (320B rows), then a frozen
   * colorbar should make ALL 120 rows byte-identical AND row0's 8 segments
   * uniform.  Report those two facts regardless of verdict. */
  OV7670_FIFO_OE_High();
  DWT_DelayMs(10u);
  OV7670_FIFO_OE_Low();
  ResetReadPointer();
  ReadFifoLineLen(line_ref, VF_ROW_BYTES);   /* row 0 */
  uint16_t identical_rows = 0u;
  uint8_t first_diff = 0xFFu;   /* 0xFF = none */
  for (uint16_t y = 1u; y < VF_ROWS; y++)
  {
    ReadFifoLineLen(line_cur, VF_ROW_BYTES);
    if (memcmp(line_ref, line_cur, VF_ROW_BYTES) == 0)
    {
      identical_rows++;
    }
    else if (first_diff == 0xFFu)
    {
      first_diff = (uint8_t)y;
    }
  }

  /* Re-read row 0 after 100ms to prove the whole frame is frozen */
  OV7670_FIFO_OE_High();
  DWT_DelayMs(100u);
  OV7670_FIFO_OE_Low();
  ResetReadPointer();
  ReadFifoLineLen(line_cur, VF_ROW_BYTES);
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

  /* Row-0 8-bar structure: 8 segments x 20px, each should be uniform */
  debug_printf("  [%s] row0 segs:", label);
  bool seg_all_uniform = true;
  uint8_t first_mixed = 0xFFu;
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
    if (!seg_uniform && first_mixed == 0xFFu)
    {
      first_mixed = (uint8_t)s;
    }
    seg_all_uniform &= seg_uniform;
    debug_printf(" s%u=%s(0x%04X)", (unsigned)s,
                 seg_uniform ? "u" : "MIXED", (unsigned)seg0);
  }
  debug_printf("\n");
  debug_printf("  [%s] row0 all8seg_uniform=%s first_mixed_seg=%u\n",
               label, seg_all_uniform ? "yes" : "no", (unsigned)first_mixed);

  /* Verdict: real 8-bar = all rows equal + frozen + 8 uniform segments
   * with mutually distinct values (excludes all-black/all-white). */
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

  /* ---- Phase D: full-frame dump for host-side analysis ----
   * Re-read the whole frozen frame (120 rows x 320B) and stream the raw
   * byte sequence, 32 bytes per line preceded by the 8-hex-digit byte
   * offset.  A byte-value histogram is accumulated in the same pass so a
   * quick distinct-bytes summary can be reported after the dump. */
  {
    OV7670_FIFO_OE_High();
    DWT_DelayMs(10u);
    OV7670_FIFO_OE_Low();
    ResetReadPointer();
    debug_printf("  [%s] FRAME_START\n", label);
    uint32_t total = (uint32_t)VF_ROWS * VF_ROW_BYTES;
    uint16_t cnt[256] = {0u};
    uint16_t distinct = 0u;
    for (uint32_t i = 0u; i < total; i++)
    {
      uint8_t b = ReadFifoByte();
      if (cnt[b] == 0u)
      {
        distinct++;
      }
      cnt[b]++;
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
    debug_printf("  [%s] FRAME_END\n", label);

    /* Quick histogram summary over the full frame */
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
#undef VF_ROW_BYTES
#undef VF_ROWS
  return verdict;
}

/* ---- Discriminating experiment: fixed-duration write window ----
 *
 * WR is held high for a FIXED 50ms, independent of VSYNC.
 * With tp=01 (walking-one) as a clean probe, we report the byte-value
 * histogram and top-4 values to verify data was written. */
static void TrialFixedWindowWrite(void)
{
  OV7670_Init();
  DWT_DelayMs(100u);
  SCCB_WriteReg(0x12u, 0x06u);   /* COM7: bit1 color bar (VGA+RGB565 base) */
  SCCB_WriteReg(0x42u, 0x08u);   /* COM17: bit3 DSP color bar */
  SCCB_WriteReg(0x70u, 0xBAu);   /* tp=01: XSC7=1, YSC7=0 (walking-one) */
  SCCB_WriteReg(0x71u, 0x35u);
  DWT_DelayMs(200u);

  OV7670_FIFO_WRST_Low();
  DWT_DelayUs(10u);
  OV7670_FIFO_WRST_High();
  OV7670_FIFO_WR_High();         /* enable write, ignoring VSYNC timing */
  DWT_DelayMs(50u);              /* fixed 50ms window */
  OV7670_FIFO_WR_Low();

  OV7670_FIFO_OE_Low();
  ResetReadPointer();

  /* Byte-value histogram over 38400B to verify data was written.
   * No assumptions about "empty" values — just report what we see. */
  uint16_t cnt[256] = {0u};
  uint16_t distinct = 0u;
  for (uint32_t i = 0u; i < 38400u; i++)
  {
    uint8_t b = ReadFifoByte();
    if (cnt[b] == 0u) distinct++;
    cnt[b]++;
  }
  uint16_t top[4] = {0u, 0u, 0u, 0u};
  uint16_t topv[4] = {0u, 0u, 0u, 0u};
  for (uint16_t v = 0u; v < 256u; v++)
  {
    if (cnt[v] == 0u) continue;
    uint16_t rank = 4u;
    while (rank > 0u && cnt[v] > top[rank - 1u])
    {
      if (rank < 4u) { top[rank] = top[rank - 1u]; topv[rank] = topv[rank - 1u]; }
      rank--;
    }
    if (rank < 4u) { top[rank] = cnt[v]; topv[rank] = v; }
  }
  debug_printf("  [fixed50ms tp=01] distinct=%u", (unsigned)distinct);
  for (uint16_t k = 0u; k < 4u; k++)
  {
    debug_printf(" [0x%02X]=%u", (unsigned)topv[k], (unsigned)top[k]);
  }
  debug_printf("\n");

  /* First 16 words of the fixed-window block */
  ResetReadPointer();
  debug_printf("  [fixed50ms tp=01] first16w:");
  for (uint16_t w = 0u; w < 16u; w++)
  {
    uint8_t hi = ReadFifoByte();
    uint8_t lo = ReadFifoByte();
    debug_printf(" %02X%02X", (unsigned)hi, (unsigned)lo);
  }
  debug_printf("\n");

  OV7670_FIFO_OE_High();
  DWT_DelayMs(20u);

  OV7670_Init();
  DWT_DelayMs(200u);
}

/* ---- Test entry ---- */

static void TestColorbarFifoData(void)
{
  /* Runtime dimension check (compile-time asserts above) */
  TEST_ASSERT_EQUAL_UINT16(LCD_TEST_WIDTH, PIPELINE_WIDTH);
  TEST_ASSERT_EQUAL_UINT16(LCD_TEST_HEIGHT, PIPELINE_HEIGHT);
  TEST_ASSERT_EQUAL_UINT16(LCD_TEST_LINE_SIZE, PIPELINE_HALF_SIZE);

  /* Datasheet (XSC[7], YSC[7]) test_pattern table is ambiguous on bit
   * order: testing only the fixed target tp=10 (XSC7=0 YSC7=1).
   * Scale values come from OV7670_Init() (XSC=0x40, YSC=0x3C) so the
   * 160x128 chain is preserved; only YSC7 is set for the test pattern. */
  const struct
  {
    uint8_t xsc;
    uint8_t ysc;
    const char *label;
  } dirs[] =
  {
    { 0x40u, 0x3Cu | 0x80u, "tp=01 XSC7=0 YSC7=1" },
  };

  bool any_8bar = false;
  // TrialFixedWindowWrite();   /* diagnostic: verify PCLK->WCLK path */
  for (uint16_t i = 0u; i < sizeof(dirs) / sizeof(dirs[0]); i++)
  {
    any_8bar |= TrialVsyncFrameFull(dirs[i].xsc, dirs[i].ysc, dirs[i].label);
  }
  TEST_ASSERT_TRUE_MESSAGE(any_8bar, "neither test_pattern direction produced a real 8-bar");
}

void RunOv7670ColorbarTests(void)
{
  UNITY_BEGIN();

  /* Self-contained init: DWT (CYCCNT timing), SCCB bus, OV7670 */
  DWT_Init();
  SCCB_Init();
  DWT_DelayMs(10u);

  /* Configure RCK as GPIO output for bit-banged FIFO read */
  FifoReadMode_Enter();

  debug_printf("[TEST_OV7670_COLORBAR] OV7670 colorbar FIFO data verification\n");

  RUN_TEST(TestColorbarInit);

  /* Let sensor stabilize after init (OV7670 datasheet: 1ms after reset) */
  DWT_DelayMs(10u);

  RUN_TEST(TestColorbarFifoData);

  /* Restore RCK to alternate function for DMA pipeline */
  FifoReadMode_Exit();

  UNITY_END();
}
