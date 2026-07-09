/**
  * @file    ov7670_sccb.c
  * @brief   SCCB (I2C-compatible) bit-bang driver for OV7670 implementation
  */
#include "ov7670_sccb.h"
#include "dwt_delay.h"
#include "main.h"
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
static void    sccb_start(void);
static void    sccb_stop(void);
static bool    sccb_write_byte(uint8_t byte);
static uint8_t sccb_read_byte(bool ack);

/**
  * @brief   Generate START condition (SDA falling edge while SCL high)
  */
static void sccb_start(void)
{
  HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_SET);
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
  HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_RESET);
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_RESET);
}

/**
  * @brief   Generate STOP condition (SDA rising edge while SCL high)
  */
static void sccb_stop(void)
{
  HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_RESET);
  DWT_DelayCycles(SCCB_T_LOW_CYC);
  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_SET);
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
  HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_SET);
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
}

/**
  * @brief   Send one byte MSB first and check ACK
  * @param   byte  Data to send
  * @retval  true   ACK received (SDA low on 9th clock)
  * @retval  false  NACK received (SDA high on 9th clock)
  */
static bool sccb_write_byte(uint8_t byte)
{
  for (uint8_t i = 0u; i < 8u; i++)
  {
    if ((byte & 0x80u) != 0u)
    {
      HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_SET);
    }
    else
    {
      HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_RESET);
    }
    DWT_DelayCycles(SCCB_T_LOW_CYC);
    HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_SET);
    DWT_DelayCycles(SCCB_T_HIGH_CYC);
    HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_RESET);
    byte <<= 1;
  }

  /* ACK phase: release SDA, clock, read ACK */
  HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_SET);
  DWT_DelayCycles(SCCB_T_LOW_CYC);
  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_SET);
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
  GPIO_PinState ack = HAL_GPIO_ReadPin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin);
  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_RESET);
  return (ack == GPIO_PIN_RESET);
}

/**
  * @brief   Read one byte MSB first and send ACK or NACK
  * @param   ack  true = send ACK (continue reading), false = send NACK (last byte)
  * @return  Byte read from slave
  */
static uint8_t sccb_read_byte(bool ack)
{
  uint8_t byte = 0u;

  for (uint8_t i = 0u; i < 8u; i++)
  {
    HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_SET);
    DWT_DelayCycles(SCCB_T_LOW_CYC);
    HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_SET);
    DWT_DelayCycles(SCCB_T_HIGH_CYC);
    byte <<= 1;
    if (HAL_GPIO_ReadPin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin) == GPIO_PIN_SET)
    {
      byte |= 0x01u;
    }
    HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_RESET);
  }

  /* ACK/NACK phase */
  if (ack)
  {
    HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_RESET);
  }
  else
  {
    HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_SET);
  }
  DWT_DelayCycles(SCCB_T_LOW_CYC);
  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_SET);
  DWT_DelayCycles(SCCB_T_HIGH_CYC);
  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_SET);
  return byte;
}

void SCCB_Init(void)
{
  HAL_GPIO_WritePin(OV7670_SCL_GPIO_Port, OV7670_SCL_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(OV7670_SDA_GPIO_Port, OV7670_SDA_Pin, GPIO_PIN_SET);
}

bool SCCB_WriteReg(uint8_t reg_addr, uint8_t data)
{
  sccb_start();
  if (!sccb_write_byte(SCCB_DEV_ADDR_W))
  {
    sccb_stop();
    return false;
  }
  if (!sccb_write_byte(reg_addr))
  {
    sccb_stop();
    return false;
  }
  if (!sccb_write_byte(data))
  {
    sccb_stop();
    return false;
  }
  sccb_stop();
  return true;
}

uint8_t SCCB_ReadReg(uint8_t reg_addr)
{
  sccb_start();
  if (!sccb_write_byte(SCCB_DEV_ADDR_W))
  {
    sccb_stop();
    return 0u;
  }
  if (!sccb_write_byte(reg_addr))
  {
    sccb_stop();
    return 0u;
  }
  sccb_start();  /* RESTART */
  if (!sccb_write_byte(SCCB_DEV_ADDR_R))
  {
    sccb_stop();
    return 0u;
  }
  uint8_t data = sccb_read_byte(false);  /* NACK = last byte */
  sccb_stop();
  return data;
}
