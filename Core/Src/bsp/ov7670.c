/**
  * @file    ov7670.c
  * @brief   OV7670 camera register configuration driver implementation
  */
#include "ov7670.h"
#include "ov7670_sccb.h"
#include "dwt_delay.h"
#include "main.h"
#include "stm32f1xx_hal.h"

/* OV7670 register addresses */
#define OV7670_REG_COM7          0x12u
#define OV7670_REG_COM3          0x0Cu
#define OV7670_REG_COM14         0x3Eu
#define OV7670_REG_COM15         0x40u
#define OV7670_REG_SCALING_DCWCTR 0x72u
#define OV7670_REG_SCALING_XSC   0x70u
#define OV7670_REG_SCALING_YSC   0x71u
#define OV7670_REG_SCALING_PCLK_DIV  0x73u
#define OV7670_REG_SCALING_PCLK_DELAY 0xA2u
#define OV7670_REG_CLKRC         0x11u
#define OV7670_REG_RGB444        0x8Cu
#define OV7670_REG_COM8          0x13u
#define OV7670_REG_COM11         0x3Bu
#define OV7670_REG_COM9          0x14u
#define OV7670_REG_MVFP          0x1Eu
#define OV7670_REG_TSLB          0x3Au
#define OV7670_REG_COM13         0x3Du

/* COM3 bit0 = color bar test mode */
#define OV7670_COM3_COLOR_BAR    0x01u
#define OV7670_COM3_DEFAULT      0x0Cu

/**
  * @brief   Register configuration table for 160x128 RGB565
  *
  *          VGA 640x480 -> DCW by2 -> 320x240
  *          -> Digital Zoom -> 160x128 (XSC=0x40, YSC=0x3C)
  */
static const struct
{
  uint8_t addr;
  uint8_t value;
} s_reg_config[] =
{
  { OV7670_REG_COM7,              0x14u }, /* QVGA + RGB565                    */
  { OV7670_REG_COM3,              0x0Cu }, /* Digital Zoom + Down Sampling     */
  { OV7670_REG_COM14,             0x18u }, /* DCW PCLK enable + manual scaling */
  { OV7670_REG_COM15,             0xD0u }, /* Full range output + RGB565       */
  { OV7670_REG_SCALING_DCWCTR,    0x11u }, /* Vertical/Horizontal by2          */
  { OV7670_REG_SCALING_XSC,       0x40u }, /* Horizontal 0.5x (320->160)       */
  { OV7670_REG_SCALING_YSC,       0x3Cu }, /* Vertical 0.533x (240->128)       */
  { OV7670_REG_SCALING_PCLK_DIV,  0x01u }, /* PCLK divider = 2                 */
  { OV7670_REG_SCALING_PCLK_DELAY, 0x02u }, /* Timing delay                    */
  { OV7670_REG_CLKRC,             0x00u }, /* No clock prescaler               */
  { OV7670_REG_RGB444,            0x00u }, /* Disable RGB444                   */
  { OV7670_REG_COM8,              0xC5u }, /* AGC/AWB/AEC enable               */
  { OV7670_REG_COM11,             0x0Au }, /* 50/60Hz auto detect              */
  { OV7670_REG_COM9,              0x4Au }, /* AGC ceiling 16x                  */
  { OV7670_REG_MVFP,              0x01u }, /* Mirror/VFlip                     */
  { OV7670_REG_TSLB,              0x0Du }, /* Output sequence default          */
  { OV7670_REG_COM13,             0x88u }, /* Gamma enable + UV swap default   */
};

bool OV7670_Init(void)
{
  /* Hardware power-up sequence */
  HAL_GPIO_WritePin(OV7670_PWDN_GPIO_Port, OV7670_PWDN_Pin, GPIO_PIN_SET);
  DWT_DelayMs(1u);
  HAL_GPIO_WritePin(OV7670_PWDN_GPIO_Port, OV7670_PWDN_Pin, GPIO_PIN_RESET);
  DWT_DelayMs(1u);
  HAL_GPIO_WritePin(OV7670_RESET_GPIO_Port, OV7670_RESET_Pin, GPIO_PIN_RESET);
  DWT_DelayMs(1u);
  HAL_GPIO_WritePin(OV7670_RESET_GPIO_Port, OV7670_RESET_Pin, GPIO_PIN_SET);
  DWT_DelayMs(1u);

  /* Initialize SCCB bus */
  SCCB_Init();

  /* Write all registers */
  for (uint8_t i = 0u; i < sizeof(s_reg_config) / sizeof(s_reg_config[0]); i++)
  {
    if (!SCCB_WriteReg(s_reg_config[i].addr, s_reg_config[i].value))
    {
      return false;
    }
  }
  return true;
}

void OV7670_EnableColorBar(void)
{
  SCCB_WriteReg(OV7670_REG_COM3, OV7670_COM3_DEFAULT | OV7670_COM3_COLOR_BAR);
}

void OV7670_DisableColorBar(void)
{
  SCCB_WriteReg(OV7670_REG_COM3, OV7670_COM3_DEFAULT);
}
