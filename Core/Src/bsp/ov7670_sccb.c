/**
  * @file    ov7670_sccb.c
  * @brief   SCCB (I2C-compatible) bit-bang driver for OV7670 implementation
  */
#include "ov7670_sccb.h"
#include "dwt_delay.h"
#include "stm32f1xx_hal.h"

/* SCCB device address: 0x42 = write, 0x43 = read */
#define SCCB_DEV_ADDR_W   0x42u
#define SCCB_DEV_ADDR_R   0x43u

/*
 * Cycle-accurate timing constants (@ 72 MHz)
 *
 * SCCB spec: f_SIO_C <= 400 kHz, t_LOW >= 1.3 us, t_HIGH >= 600 ns
 *
 * Delay values are CYCCNT ticks; GPIO operation overhead (HAL function
 * calls ~10-15 cycles each) adds on top, so the programmed delay is
 * shorter than the target to avoid exceeding the 400 kHz limit.
 *
 *   t_LOW:  100 cycles delay + ~20 cycles GPIO = ~120 cyc = 1.67 us (>= 1.3 us)
 *   t_HIGH:  50 cycles delay + ~20 cycles GPIO =  ~70 cyc = 0.97 us (>= 600 ns)
 *   Total:  ~190 cycles = 2.64 us -> ~379 kHz (<= 400 kHz)
 */
#define SCCB_T_LOW_CYC    100u
#define SCCB_T_HIGH_CYC    50u

/* Static function prototypes */
static void    GenStart(void);
static void    GenStop(void);
static bool    WriteByte(uint8_t byte);
static uint8_t ReadByte(bool ack);

/**
  * @brief   Generate START condition (SDA falling edge while SCL high)
  */
static void GenStart(void)
{
  SCCB_SDA_High();
  SCCB_SCL_High();
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
  SCCB_SDA_Low();
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
  SCCB_SCL_Low();
}

/**
  * @brief   Generate STOP condition (SDA rising edge while SCL high)
  */
static void GenStop(void)
{
  SCCB_SDA_Low();
  SCCB_SCL_Low();
  DWT_DelayCycles(SCCB_T_LOW_CYC);
  SCCB_SCL_High();
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
  SCCB_SDA_High();
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
}

/**
  * @brief   Send one byte MSB first and check ACK
  * @param   byte  Data to send
  * @retval  true   ACK received (SDA low on 9th clock)
  * @retval  false  NACK received (SDA high on 9th clock)
  */
static bool WriteByte(uint8_t byte)
{
  for (uint8_t i = 0u; i < 8u; i++)
  {
    if ((byte & 0x80u) != 0u)
    {
      SCCB_SDA_High();
    }
    else
    {
      SCCB_SDA_Low();
    }
    DWT_DelayCycles(SCCB_T_LOW_CYC);
    SCCB_SCL_High();
    DWT_DelayCycles(SCCB_T_HIGH_CYC);
    SCCB_SCL_Low();
    byte <<= 1;
  }

  /* ACK phase: release SDA, clock, read ACK */
  SCCB_SDA_High();
  DWT_DelayCycles(SCCB_T_LOW_CYC);
  SCCB_SCL_High();
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
  GPIO_PinState ack = SCCB_SDA_Read();
  SCCB_SCL_Low();
  return (ack == GPIO_PIN_RESET);
}

/**
  * @brief   Read one byte MSB first and send ACK or NACK
  * @param   ack  true = send ACK (continue reading), false = send NACK (last byte)
  * @return  Byte read from slave
  */
static uint8_t ReadByte(bool ack)
{
  uint8_t byte = 0u;

  for (uint8_t i = 0u; i < 8u; i++)
  {
    SCCB_SDA_High();
    DWT_DelayCycles(SCCB_T_LOW_CYC);
    SCCB_SCL_High();
    DWT_DelayCycles(SCCB_T_HIGH_CYC);
    byte <<= 1;
    if (SCCB_SDA_Read() == GPIO_PIN_SET)
    {
      byte |= 0x01u;
    }
    SCCB_SCL_Low();
  }

  /* ACK/NACK phase */
  if (ack)
  {
    SCCB_SDA_Low();
  }
  else
  {
    SCCB_SDA_High();
  }
  DWT_DelayCycles(SCCB_T_LOW_CYC);
  SCCB_SCL_High();
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
  SCCB_SCL_Low();
  SCCB_SDA_High();
  return byte;
}

void SCCB_Init(void)
{
  SCCB_SCL_High();
  SCCB_SDA_High();
}

bool SCCB_WriteReg(uint8_t reg_addr, uint8_t data)
{
  bool ok = false;

  GenStart();
  if (!WriteByte(SCCB_DEV_ADDR_W)) goto cleanup;
  if (!WriteByte(reg_addr))        goto cleanup;
  if (!WriteByte(data))            goto cleanup;
  ok = true;

cleanup:
  GenStop();
  return ok;
}

uint8_t SCCB_ReadReg(uint8_t reg_addr)
{
  uint8_t data = 0u;

  GenStart();
  if (!WriteByte(SCCB_DEV_ADDR_W)) goto cleanup;
  if (!WriteByte(reg_addr))        goto cleanup;
  GenStart();  /* RESTART */
  if (!WriteByte(SCCB_DEV_ADDR_R)) goto cleanup;
  data = ReadByte(false);  /* NACK = last byte */

cleanup:
  GenStop();
  return data;
}
