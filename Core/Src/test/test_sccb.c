/**
  * @file    test_sccb.c
  * @brief   TEST_SCCB group - SCCB bus & wiring verification
  *
  *          Verifies SCCB communication with OV7670 by reading
  *          identity registers (PID, VER, MIDH, MIDL) and checking
  *          stability over consecutive reads.
  */
#include "test_sccb.h"
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

static void test_sccb_read_pid(void)
{
  debug_printf("  Reading PID (0x%02X)...\n", SCCB_REG_PID);
  uint8_t pid = ReadRegChecked(SCCB_REG_PID);
  debug_printf("  PID = 0x%02X (expected 0x76)\n", pid);
  TEST_ASSERT_EQUAL_UINT8(0x76u, pid);
}

static void test_sccb_read_ver(void)
{
  debug_printf("  Reading VER (0x%02X)...\n", SCCB_REG_VER);
  uint8_t ver = ReadRegChecked(SCCB_REG_VER);
  debug_printf("  VER = 0x%02X (expected 0x73)\n", ver);
  TEST_ASSERT_EQUAL_UINT8(0x73u, ver);
}

static void test_sccb_read_midh(void)
{
  debug_printf("  Reading MIDH (0x%02X)...\n", SCCB_REG_MIDH);
  uint8_t midh = ReadRegChecked(SCCB_REG_MIDH);
  debug_printf("  MIDH = 0x%02X (expected 0x7A)\n", midh);
  TEST_ASSERT_EQUAL_UINT8(0x7Au, midh);
}

static void test_sccb_read_midl(void)
{
  debug_printf("  Reading MIDL (0x%02X)...\n", SCCB_REG_MIDL);
  uint8_t midl = ReadRegChecked(SCCB_REG_MIDL);
  debug_printf("  MIDL = 0x%02X (expected 0xA2)\n", midl);
  TEST_ASSERT_EQUAL_UINT8(0xA2u, midl);
}

static void test_sccb_read_stability(void)
{
  debug_printf("  Reading PID 5 times for stability...\n");
  uint8_t values[5];
  for (int i = 0; i < 5; i++)
  {
    values[i] = ReadRegChecked(SCCB_REG_PID);
    DWT_DelayMs(10u);
    debug_printf("    Read %d: 0x%02X\n", i, values[i]);
  }
  for (int i = 1; i < 5; i++)
  {
    TEST_ASSERT_EQUAL_UINT8(values[0], values[i]);
  }
}

/* ---- Run all TEST_SCCB tests ---- */

void RunSccbTests(void)
{
  UNITY_BEGIN();

  /* Initialize DWT (SCCB timing depends on CYCCNT) and SCCB bus */
  DWT_Init();
  SCCB_Init();
  DWT_DelayMs(10u);  /* let bus settle */

  debug_printf("[TEST_SCCB] SCCB bus & wiring verification\n");

  RUN_TEST(test_sccb_read_pid);
  RUN_TEST(test_sccb_read_ver);
  RUN_TEST(test_sccb_read_midh);
  RUN_TEST(test_sccb_read_midl);
  RUN_TEST(test_sccb_read_stability);

  UNITY_END();
}
