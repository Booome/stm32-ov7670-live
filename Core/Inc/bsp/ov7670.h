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

/** @brief  Initialize OV7670: hardware reset + SCCB register config
  * @retval true   All registers written successfully
  * @retval false  Camera not responding (SCCB NACK)
  */
bool    OV7670_Init(void);

/** @brief  Enable color bar test pattern (COM3 bit0 = 1) */
void    OV7670_EnableColorBar(void);

/** @brief  Disable color bar test pattern (COM3 bit0 = 0) */
void    OV7670_DisableColorBar(void);

#endif /* OV7670_H */
