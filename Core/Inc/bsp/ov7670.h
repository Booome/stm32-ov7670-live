/**
  * @file    ov7670.h
  * @brief   OV7670 camera register configuration driver
  *
  *          Handles hardware power-up sequence (PWDN/RESET) and
  *          SCCB register configuration for 160x128 RGB565 output.
  *          Depends on SCCB module (requires SCCB_Init before use).
  */
#ifndef OV7670_H
#define OV7670_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "stm32f1xx_hal.h"

/* ---- OV7670 pin control (static inline) ---- */

/** @brief Power down camera (PWDN high) */
static inline void OV7670_PWDN_High(void)
{
  OV7670_PWDN_GPIO_Port->BSRR = OV7670_PWDN_Pin;
}

/** @brief Power on camera (PWDN low) */
static inline void OV7670_PWDN_Low(void)
{
  OV7670_PWDN_GPIO_Port->BRR = OV7670_PWDN_Pin;
}

/** @brief Assert camera reset (RESET low) */
static inline void OV7670_RESET_Low(void)
{
  OV7670_RESET_GPIO_Port->BRR = OV7670_RESET_Pin;
}

/** @brief Release camera reset (RESET high) */
static inline void OV7670_RESET_High(void)
{
  OV7670_RESET_GPIO_Port->BSRR = OV7670_RESET_Pin;
}

/** @brief Assert FIFO write pointer reset (WRST low) */
static inline void OV7670_FIFO_WRST_Low(void)
{
  OV7670_FIFO_WRST_GPIO_Port->BRR = OV7670_FIFO_WRST_Pin;
}

/** @brief Release FIFO write pointer reset (WRST high) */
static inline void OV7670_FIFO_WRST_High(void)
{
  OV7670_FIFO_WRST_GPIO_Port->BSRR = OV7670_FIFO_WRST_Pin;
}

/** @brief Assert FIFO read pointer reset (RRST low) */
static inline void OV7670_FIFO_RRST_Low(void)
{
  OV7670_FIFO_RRST_GPIO_Port->BRR = OV7670_FIFO_RRST_Pin;
}

/** @brief Release FIFO read pointer reset (RRST high) */
static inline void OV7670_FIFO_RRST_High(void)
{
  OV7670_FIFO_RRST_GPIO_Port->BSRR = OV7670_FIFO_RRST_Pin;
}

/** @brief Enable FIFO output (OE low) */
static inline void OV7670_FIFO_OE_Low(void)
{
  OV7670_FIFO_OE_GPIO_Port->BRR = OV7670_FIFO_OE_Pin;
}

/** @brief Disable FIFO output (OE high) */
static inline void OV7670_FIFO_OE_High(void)
{
  OV7670_FIFO_OE_GPIO_Port->BSRR = OV7670_FIFO_OE_Pin;
}

/** @brief Enable FIFO write (WR high, NAND gate active) */
static inline void OV7670_FIFO_WR_High(void)
{
  OV7670_FIFO_WR_GPIO_Port->BSRR = OV7670_FIFO_WR_Pin;
}

/** @brief Disable FIFO write (WR low) */
static inline void OV7670_FIFO_WR_Low(void)
{
  OV7670_FIFO_WR_GPIO_Port->BRR = OV7670_FIFO_WR_Pin;
}

/* ---- Public API ---- */

/* ---- Tunable image parameters (may need per-module adjustment) ----
 * AWB gain registers do not affect sensor colorbar (injected after AWB);
 * they affect real-camera images only.  Contrast default = 0x40. */
#define OV7670_AWB_GGAIN    0x40u
#define OV7670_AWB_BLUE     0x40u
#define OV7670_AWB_RED      0x60u
#define OV7670_CONTRAS      0x40u
#define OV7670_COM13_VAL    0xC0u  /* gamma on, UVSAT on */
#define OV7670_COM6_VAL     0x43u  /* ABLC off */

/** @brief  Initialize OV7670: hardware reset + SCCB register config
  * @retval true   All registers written successfully
  * @retval false  Camera not responding (SCCB NACK)
  */
bool    OV7670_Init(void);

/** @brief  Enable 8-bar color bar test pattern (COM7 bit1 + COM17 bit3 + XSC7) */
void    OV7670_EnableColorBar(void);

/** @brief  Disable color bar test pattern (restore COM7/COM17/XSC/YSC defaults) */
void    OV7670_DisableColorBar(void);

#endif /* OV7670_H */
