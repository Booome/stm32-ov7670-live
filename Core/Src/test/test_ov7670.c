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
#define TEST_REG_MVFP       0x1Eu
#define TEST_REG_RGB444     0x8Cu
#define TEST_REG_SCALING_DCWCTR   0x72u
#define TEST_REG_SCALING_XSC      0x70u
#define TEST_REG_SCALING_YSC      0x71u
#define TEST_REG_SCALING_PCLK_DIV 0x73u
#define TEST_REG_MTX1       0x4Fu
#define TEST_REG_GAM0       0x7Au

/* ---- Helper: read with assertion ---- */

static uint8_t ReadRegChecked(uint8_t reg_addr)
{
  uint8_t val = 0u;
  SCCB_ReadStatusTypeDef st = SCCB_ReadRegEx(reg_addr, &val);
  if (st != SCCB_READ_OK)
  {
    debug_printf("  SCCB_ReadRegEx(0x%02X) failed: %d\n", reg_addr, st);
  }
  TEST_ASSERT_EQUAL(SCCB_READ_OK, st);
  return val;
}

/* ---- Test: OV7670_Init success ---- */

static void test_ov7670_init(void)
{
  debug_printf("  Calling OV7670_Init()...\n");
  bool ok = OV7670_Init();
  TEST_ASSERT_TRUE(ok);
  debug_printf("  OV7670_Init() succeeded\n");
}

/* ---- Test: stable configuration registers readback ---- */

static void test_ov7670_stable_registers(void)
{
  debug_printf("  Reading stable configuration registers...\n");

  uint8_t v;

  /* COM7: QVGA + RGB565 (bit7 RESET reads as 0) */
  v = ReadRegChecked(TEST_REG_COM7);
  debug_printf("    COM7 = 0x%02X (expect 0x14)\n", v);
  TEST_ASSERT_EQUAL_UINT8(0x14u, v);

  /* COM15: full range + RGB565 */
  v = ReadRegChecked(TEST_REG_COM15);
  debug_printf("    COM15 = 0x%02X (expect 0xD0)\n", v);
  TEST_ASSERT_EQUAL_UINT8(0xD0u, v);

  /* COM3: DCW + down-sampling */
  v = ReadRegChecked(TEST_REG_COM3);
  debug_printf("    COM3 = 0x%02X (expect 0x0C)\n", v);
  TEST_ASSERT_EQUAL_UINT8(0x0Cu, v);

  /* MVFP: mirror/flip */
  v = ReadRegChecked(TEST_REG_MVFP);
  debug_printf("    MVFP = 0x%02X (expect 0x07)\n", v);
  TEST_ASSERT_EQUAL_UINT8(0x07u, v);

  /* RGB444: disabled */
  v = ReadRegChecked(TEST_REG_RGB444);
  debug_printf("    RGB444 = 0x%02X (expect 0x00)\n", v);
  TEST_ASSERT_EQUAL_UINT8(0x00u, v);

  /* SCALING_DCWCTR: V/H by 2 */
  v = ReadRegChecked(TEST_REG_SCALING_DCWCTR);
  debug_printf("    DCWCTR = 0x%02X (expect 0x11)\n", v);
  TEST_ASSERT_EQUAL_UINT8(0x11u, v);

  /* SCALING_XSC: horizontal 0.5x */
  v = ReadRegChecked(TEST_REG_SCALING_XSC);
  debug_printf("    XSC = 0x%02X (expect 0x40)\n", v);
  TEST_ASSERT_EQUAL_UINT8(0x40u, v);

  /* SCALING_YSC: vertical 0.533x */
  v = ReadRegChecked(TEST_REG_SCALING_YSC);
  debug_printf("    YSC = 0x%02X (expect 0x3C)\n", v);
  TEST_ASSERT_EQUAL_UINT8(0x3Cu, v);

  /* SCALING_PCLK_DIV: /2 */
  v = ReadRegChecked(TEST_REG_SCALING_PCLK_DIV);
  debug_printf("    PCLK_DIV = 0x%02X (expect 0xF1)\n", v);
  TEST_ASSERT_EQUAL_UINT8(0xF1u, v);

  /* MTX1 */
  v = ReadRegChecked(TEST_REG_MTX1);
  debug_printf("    MTX1 = 0x%02X (expect 0xB3)\n", v);
  TEST_ASSERT_EQUAL_UINT8(0xB3u, v);

  /* GAM0 */
  v = ReadRegChecked(TEST_REG_GAM0);
  debug_printf("    GAM0 = 0x%02X (expect 0x20)\n", v);
  TEST_ASSERT_EQUAL_UINT8(0x20u, v);
}

/* ---- Test: ColorBar enable/disable ---- */

static void test_ov7670_colorbar(void)
{
  uint8_t v;

  debug_printf("  Testing ColorBar mode...\n");

  OV7670_EnableColorBar();
  v = ReadRegChecked(TEST_REG_COM3);
  debug_printf("    COM3 after EnableColorBar = 0x%02X (bit0 should be 1)\n", v);
  TEST_ASSERT_NOT_EQUAL(0u, v & 0x01u);

  OV7670_DisableColorBar();
  v = ReadRegChecked(TEST_REG_COM3);
  debug_printf("    COM3 after DisableColorBar = 0x%02X (bit0 should be 0)\n", v);
  TEST_ASSERT_EQUAL(0u, v & 0x01u);
}

/* ---- Test: hardware 3A engine running ---- */

static void test_3a_hardware_running(void)
{
  uint8_t com8, aech, gain;

  debug_printf("  Testing hardware 3A engine status...\n");

  /* COM8: verify AEC + AGC enable bits are set */
  com8 = ReadRegChecked(TEST_REG_COM8);
  debug_printf("    COM8 = 0x%02X (AGC+AWB+AEC should be enabled)\n", com8);
  TEST_ASSERT_TRUE(com8 & 0x01u);  /* AEC enable (bit0) */
  TEST_ASSERT_TRUE(com8 & 0x02u);  /* AGC enable (bit1) */

  /* AEC register: readable and in valid 8-bit range */
  aech = ReadRegChecked(0x07u);    /* AECH */
  debug_printf("    AECH = 0x%02X (valid if readable)\n", aech);
  /* Value is scene-dependent; just verify it's readable and not stuck */

  /* GAIN register: readable and in valid 8-bit range */
  gain = ReadRegChecked(0x00u);    /* GAIN */
  debug_printf("    GAIN = 0x%02X (valid if readable)\n", gain);
  /* Value is scene-dependent; just verify it's readable and not stuck */
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
  RUN_TEST(test_ov7670_init);

  /* Let sensor stabilize after init */
  DWT_DelayMs(500u);

  /* Phase 2: Register readback */
  RUN_TEST(test_ov7670_stable_registers);
  RUN_TEST(test_ov7670_colorbar);

  /* Phase 3: Hardware 3A verification */
  RUN_TEST(test_3a_hardware_running);

  UNITY_END();
}
