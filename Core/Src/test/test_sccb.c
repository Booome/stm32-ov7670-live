/**
  * @file    test_sccb.c
  * @brief   TEST_SCCB group - SCCB bus & wiring verification
  *
  *          Verifies SCCB communication with OV7670 by reading
  *          identity registers (PID, VER, MIDH, MIDL) and checking
  *          stability over consecutive reads.
  */
#include "test_sccb.h"
#include "ov7670.h"
#include "ov7670_sccb.h"
#include "dwt_delay.h"
#include "unity.h"
#include "debug.h"
#include "stm32f1xx_hal.h"

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

/* ---- Test cases ---- */

static void TestSccbReadPid(void)
{
  TEST_ASSERT_EQUAL_UINT8(0x76u, ReadRegChecked(SCCB_REG_PID));
}

static void TestSccbReadVer(void)
{
  TEST_ASSERT_EQUAL_UINT8(0x73u, ReadRegChecked(SCCB_REG_VER));
}

static void TestSccbReadMidh(void)
{
  TEST_ASSERT_EQUAL_UINT8(0x7Fu, ReadRegChecked(SCCB_REG_MIDH));
}

static void TestSccbReadMidl(void)
{
  TEST_ASSERT_EQUAL_UINT8(0xA2u, ReadRegChecked(SCCB_REG_MIDL));
}

static void TestSccbReadStability(void)
{
  uint8_t values[5];
  for (uint8_t i = 0u; i < 5u; i++)
  {
    values[i] = ReadRegChecked(SCCB_REG_PID);
    DWT_DelayMs(10u);
  }
  for (uint8_t i = 1u; i < 5u; i++)
  {
    TEST_ASSERT_EQUAL_UINT8(values[0], values[i]);
  }
}

/* ---- Diagnostic: verify GPIO can drive SCL/SDA low ---- */

static void TestGpioDriveSclSda(void)
{
  GPIO_PinState rd;

  /* SCL test (read via HAL even though pin is output) */
  SCCB_SCL_High();
  DWT_DelayMs(1u);
  rd = HAL_GPIO_ReadPin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin);
  TEST_ASSERT_EQUAL(GPIO_PIN_SET, rd);

  SCCB_SCL_Low();
  DWT_DelayMs(1u);
  rd = HAL_GPIO_ReadPin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin);
  TEST_ASSERT_EQUAL(GPIO_PIN_RESET, rd);

  SCCB_SCL_High();
  DWT_DelayMs(1u);

  /* SDA test */
  SCCB_SDA_High();
  DWT_DelayMs(1u);
  rd = SCCB_SDA_Read();
  TEST_ASSERT_EQUAL(GPIO_PIN_SET, rd);

  SCCB_SDA_Low();
  DWT_DelayMs(1u);
  rd = SCCB_SDA_Read();
  TEST_ASSERT_EQUAL(GPIO_PIN_RESET, rd);

  SCCB_SDA_High();
  DWT_DelayMs(1u);
}

/* ---- Run all TEST_SCCB tests ---- */

void RunSccbTests(void)
{
  UNITY_BEGIN();

  /* Initialize DWT (SCCB timing depends on CYCCNT) */
  DWT_Init();

  /* OV7670 hardware power-up reset sequence (same as OV7670_Init) */
  OV7670_PWDN_High();
  DWT_DelayMs(1u);
  OV7670_PWDN_Low();
  DWT_DelayMs(1u);
  OV7670_RESET_Low();
  DWT_DelayMs(1u);
  OV7670_RESET_High();
  DWT_DelayMs(1u);

  /* Initialize SCCB bus */
  SCCB_Init();
  DWT_DelayMs(10u);  /* let bus settle */

  debug_printf("[TEST_SCCB] SCCB bus & wiring verification\n");

  RUN_TEST(TestGpioDriveSclSda);

  RUN_TEST(TestSccbReadPid);
  RUN_TEST(TestSccbReadVer);
  RUN_TEST(TestSccbReadMidh);
  RUN_TEST(TestSccbReadMidl);
  RUN_TEST(TestSccbReadStability);

  UNITY_END();
}
