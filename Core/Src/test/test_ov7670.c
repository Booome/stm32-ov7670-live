/**
  * @file    test_ov7670.c
  * @brief   TEST_OV7670 group - OV7670 register readback verification
  *
  *          Verifies that OV7670_Init() completes successfully and
  *          key configuration registers read back the expected values.
  *          Also tests ColorBar mode and hardware 3A engine status.
  */
#include "test_ov7670.h"
#include "ov7670.h"
#include "ov7670_sccb.h"
#include "dwt_delay.h"
#include "unity.h"
#include "debug.h"
#include "stm32f1xx_hal.h"

/* ---- OV7670 register addresses (mirrored from ov7670.c internals) ---- */

#define TEST_REG_COM7       0x12u
#define TEST_REG_COM8       0x13u
#define TEST_REG_COM3       0x0Cu
#define TEST_REG_COM15      0x40u
#define TEST_REG_COM17      0x42u
#define TEST_REG_MVFP       0x1Eu
#define TEST_REG_RGB444     0x8Cu
#define TEST_REG_SCALING_DCWCTR   0x72u
#define TEST_REG_SCALING_XSC      0x70u
#define TEST_REG_SCALING_YSC      0x71u
#define TEST_REG_SCALING_PCLK_DIV 0x73u
#define TEST_REG_MTX1       0x4Fu
#define TEST_REG_GAM0       0x7Au
#define TEST_REG_GAIN       0x00u
#define TEST_REG_AECH       0x07u

/* ---- Status code to string for debug output ---- */

static const char *StatusToString(SCCB_ReadStatusTypeDef s)
{
  switch (s)
  {
    case SCCB_READ_OK:         return "OK";
    case SCCB_READ_BUS_BUSY:   return "BUS_BUSY (SDA stuck low)";
    case SCCB_READ_NACK_ADDR:  return "NACK_ADDR (device not responding)";
    case SCCB_READ_NACK_REG:   return "NACK_REG (reg addr NACK)";
    case SCCB_READ_NACK_RADDR: return "NACK_RADDR (read addr NACK)";
    default:                   return "?";
  }
}

/* ---- Helper: read with assertion ---- */

static uint8_t ReadRegChecked(uint8_t reg_addr)
{
  uint8_t val = 0u;
  SCCB_ReadStatusTypeDef st = SCCB_ReadRegEx(reg_addr, &val);
  if (st != SCCB_READ_OK)
  {
    debug_printf("  SCCB_ReadRegEx(0x%02X) failed: %s\n", reg_addr, StatusToString(st));
  }
  TEST_ASSERT_EQUAL(SCCB_READ_OK, st);
  return val;
}

/* ---- Test: OV7670_Init success ---- */

static void TestOv7670Init(void)
{
  TEST_ASSERT_TRUE(OV7670_Init());
}

/* ---- Test: stable configuration registers readback ---- */

static void TestOv7670StableRegisters(void)
{
  /* COM7: QVGA + RGB565 (bit7 RESET reads as 0) */
  TEST_ASSERT_EQUAL_UINT8(0x14u, ReadRegChecked(TEST_REG_COM7));

  /* COM15: full range + RGB565 */
  TEST_ASSERT_EQUAL_UINT8(0xD0u, ReadRegChecked(TEST_REG_COM15));

  /* COM3: DCW + down-sampling */
  TEST_ASSERT_EQUAL_UINT8(0x0Cu, ReadRegChecked(TEST_REG_COM3));

  /* MVFP: mirror/flip */
  TEST_ASSERT_EQUAL_UINT8(0x07u, ReadRegChecked(TEST_REG_MVFP));

  /* RGB444: disabled */
  TEST_ASSERT_EQUAL_UINT8(0x00u, ReadRegChecked(TEST_REG_RGB444));

  /* SCALING_DCWCTR: V/H by 2 */
  TEST_ASSERT_EQUAL_UINT8(0x11u, ReadRegChecked(TEST_REG_SCALING_DCWCTR));

  /* SCALING_XSC: horizontal 0.5x */
  TEST_ASSERT_EQUAL_UINT8(0x40u, ReadRegChecked(TEST_REG_SCALING_XSC));

  /* SCALING_YSC: vertical 0.533x */
  TEST_ASSERT_EQUAL_UINT8(0x3Cu, ReadRegChecked(TEST_REG_SCALING_YSC));

  /* SCALING_PCLK_DIV: /2 */
  TEST_ASSERT_EQUAL_UINT8(0xF1u, ReadRegChecked(TEST_REG_SCALING_PCLK_DIV));

  /* MTX1 */
  TEST_ASSERT_EQUAL_UINT8(0xB3u, ReadRegChecked(TEST_REG_MTX1));

  /* GAM0 */
  TEST_ASSERT_EQUAL_UINT8(0x20u, ReadRegChecked(TEST_REG_GAM0));
}

/* ---- Test: ColorBar enable/disable ---- */

static void TestOv7670Colorbar(void)
{
  /* Enable: COM7 bit1, COM17 bit3, XSC bit7 (test_pattern "10" = 8-bar) */
  OV7670_EnableColorBar();
  TEST_ASSERT_NOT_EQUAL(0u, ReadRegChecked(TEST_REG_COM7) & 0x02u);
  TEST_ASSERT_NOT_EQUAL(0u, ReadRegChecked(TEST_REG_COM17) & 0x08u);
  TEST_ASSERT_NOT_EQUAL(0u, ReadRegChecked(TEST_REG_SCALING_XSC) & 0x80u);
  TEST_ASSERT_EQUAL(0u, ReadRegChecked(TEST_REG_SCALING_YSC) & 0x80u);

  /* Disable: all color bar bits cleared, defaults restored */
  OV7670_DisableColorBar();
  TEST_ASSERT_EQUAL(0u, ReadRegChecked(TEST_REG_COM7) & 0x02u);
  TEST_ASSERT_EQUAL(0u, ReadRegChecked(TEST_REG_COM17) & 0x08u);
  TEST_ASSERT_EQUAL(0u, ReadRegChecked(TEST_REG_SCALING_XSC) & 0x80u);
  TEST_ASSERT_EQUAL(0u, ReadRegChecked(TEST_REG_SCALING_YSC) & 0x80u);
}

/* ---- Test: hardware 3A engine running ---- */

static void TestHardware3aRunning(void)
{
  uint8_t com8 = ReadRegChecked(TEST_REG_COM8);

  /* COM8: AEC (bit0) and AGC (bit1) enable bits must be set */
  TEST_ASSERT_TRUE(com8 & 0x01u);
  TEST_ASSERT_TRUE(com8 & 0x02u);

  /* GAIN/AECH are scene-dependent; just verify they're readable */
  ReadRegChecked(TEST_REG_GAIN);
  ReadRegChecked(TEST_REG_AECH);
}

/* ---- Run all TEST_OV7670 tests ---- */

void RunOv7670Tests(void)
{
  UNITY_BEGIN();

  /* Initialize DWT (SCCB timing depends on CYCCNT) and SCCB bus */
  DWT_Init();
  SCCB_Init();
  DWT_DelayMs(10u);

  debug_printf("[TEST_OV7670] OV7670 register verification\n");

  /* Phase 1: Init must succeed before any register tests */
  RUN_TEST(TestOv7670Init);

  /* Let sensor stabilize after init */
  DWT_DelayMs(500u);

  /* Phase 2: Register readback */
  RUN_TEST(TestOv7670StableRegisters);
  RUN_TEST(TestOv7670Colorbar);

  /* Phase 3: Hardware 3A verification */
  RUN_TEST(TestHardware3aRunning);

  UNITY_END();
}
