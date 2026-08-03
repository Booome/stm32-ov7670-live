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
  debug_printf("  Reading PID (0x%02X)...\n", SCCB_REG_PID);
  uint8_t pid = ReadRegChecked(SCCB_REG_PID);
  debug_printf("  PID = 0x%02X (expected 0x76)\n", pid);
  TEST_ASSERT_EQUAL_UINT8(0x76u, pid);
}

static void TestSccbReadVer(void)
{
  debug_printf("  Reading VER (0x%02X)...\n", SCCB_REG_VER);
  uint8_t ver = ReadRegChecked(SCCB_REG_VER);
  debug_printf("  VER = 0x%02X (expected 0x73)\n", ver);
  TEST_ASSERT_EQUAL_UINT8(0x73u, ver);
}

static void TestSccbReadMidh(void)
{
  debug_printf("  Reading MIDH (0x%02X)...\n", SCCB_REG_MIDH);
  uint8_t midh = ReadRegChecked(SCCB_REG_MIDH);
  debug_printf("  MIDH = 0x%02X (expected 0x7A)\n", midh);
  TEST_ASSERT_EQUAL_UINT8(0x7Au, midh);
}

static void TestSccbReadMidl(void)
{
  debug_printf("  Reading MIDL (0x%02X)...\n", SCCB_REG_MIDL);
  uint8_t midl = ReadRegChecked(SCCB_REG_MIDL);
  debug_printf("  MIDL = 0x%02X (expected 0xA2)\n", midl);
  TEST_ASSERT_EQUAL_UINT8(0xA2u, midl);
}

static void TestSccbReadStability(void)
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

/* ---- Diagnostic: scan all possible I2C/SCCB addresses ---- */

/* Must include low-level SCCB internals for direct WriteByte access.
   Since WriteByte is static in ov7670_sccb.c, we duplicate the
   minimal bit-bang logic here for diagnostic purposes only. */

#include "dwt_delay.h"

#define DIAG_SCL_High()  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_SET)
#define DIAG_SCL_Low()   HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_RESET)
#define DIAG_SDA_High()  HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_SET)
#define DIAG_SDA_Low()   HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_RESET)
#define DIAG_SDA_Read()  HAL_GPIO_ReadPin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin)

#define DIAG_T_LOW   100u
#define DIAG_T_HIGH   50u

static bool DiagWriteByte(uint8_t byte)
{
  for (uint8_t i = 0u; i < 8u; i++)
  {
    if ((byte & 0x80u) != 0u) { DIAG_SDA_High(); }
    else                      { DIAG_SDA_Low(); }
    DWT_DelayCycles(DIAG_T_LOW);
    DIAG_SCL_High();
    DWT_DelayCycles(DIAG_T_HIGH);
    DIAG_SCL_Low();
    byte <<= 1;
  }
  DIAG_SDA_High();
  DWT_DelayCycles(DIAG_T_LOW);
  DIAG_SCL_High();
  DWT_DelayCycles(DIAG_T_HIGH);
  GPIO_PinState ack = DIAG_SDA_Read();
  DIAG_SCL_Low();
  return (ack == GPIO_PIN_RESET);
}

static void DiagStart(void)
{
  DIAG_SDA_High();
  DIAG_SCL_High();
  DWT_DelayCycles(DIAG_T_HIGH);
  DIAG_SDA_Low();
  DWT_DelayCycles(DIAG_T_HIGH);
  DIAG_SCL_Low();
}

static void DiagStop(void)
{
  DIAG_SDA_Low();
  DIAG_SCL_Low();
  DWT_DelayCycles(DIAG_T_LOW);
  DIAG_SCL_High();
  DWT_DelayCycles(DIAG_T_HIGH);
  DIAG_SDA_High();
  DWT_DelayCycles(DIAG_T_HIGH);
}

static void TestSccbAddrScan(void)
{
  /* OV7670 SCCB device address (8-bit write form, confirmed from all reference designs) */
  const uint8_t addr = 0x42u;

  debug_printf("  Probing SCCB address 0x%02X...\n", addr);
  DiagStart();
  bool ack = DiagWriteByte(addr);
  DiagStop();
  debug_printf("    0x%02X -> %s\n", addr, ack ? "ACK" : "NACK");
  /* This test always passes; it's diagnostic only */
  TEST_PASS();
}

/* ---- Diagnostic: verify GPIO can drive SCL/SDA low ---- */

static void TestGpioDriveSclSda(void)
{
  GPIO_PinState rd;

  debug_printf("  Verifying GPIO drive on SCL (PB10) and SDA (PB11)...\n");

  /* -- SCL test (read via HAL even though pin is output) -- */
  SCCB_SCL_High();
  DWT_DelayMs(1u);
  rd = HAL_GPIO_ReadPin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin);
  debug_printf("    SCL idle high: %s\n", (rd == GPIO_PIN_SET) ? "OK" : "FAIL");
  TEST_ASSERT_EQUAL(GPIO_PIN_SET, rd);

  SCCB_SCL_Low();
  DWT_DelayMs(1u);
  rd = HAL_GPIO_ReadPin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin);
  debug_printf("    SCL driven low: %s (read=%d)\n",
               (rd == GPIO_PIN_RESET) ? "OK" : "FAIL", rd);
  TEST_ASSERT_EQUAL(GPIO_PIN_RESET, rd);

  SCCB_SCL_High();
  DWT_DelayMs(1u);

  /* -- SDA test -- */
  SCCB_SDA_High();
  DWT_DelayMs(1u);
  rd = SCCB_SDA_Read();
  debug_printf("    SDA idle high: %s\n", (rd == GPIO_PIN_SET) ? "OK" : "FAIL");
  TEST_ASSERT_EQUAL(GPIO_PIN_SET, rd);

  SCCB_SDA_Low();
  DWT_DelayMs(1u);
  rd = SCCB_SDA_Read();
  debug_printf("    SDA driven low: %s (read=%d)\n",
               (rd == GPIO_PIN_RESET) ? "OK" : "FAIL", rd);
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

  /* OV7670 hardware power-up reset sequence */
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
  RUN_TEST(TestSccbAddrScan);

  RUN_TEST(TestSccbReadPid);
  RUN_TEST(TestSccbReadVer);
  RUN_TEST(TestSccbReadMidh);
  RUN_TEST(TestSccbReadMidl);
  RUN_TEST(TestSccbReadStability);

  UNITY_END();
}
