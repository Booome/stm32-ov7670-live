/**
  * @file    ov7670.c
  * @brief   OV7670 camera register configuration driver implementation
  */
#include "ov7670.h"
#include "ov7670_sccb.h"
#include "dwt_delay.h"
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
  *
  *          Based on Linux kernel ov7670.c defaults + bayernfan CameraInit,
  *          verified against LiveOV7670 (Indrek Luuk).
  */
static const struct
{
  uint8_t addr;
  uint8_t value;
} s_reg_config[] =
{
  /* ---- Clock ---- */
  { OV7670_REG_CLKRC,             0x00u }, /* CLKRC: no prescaler              */

  /* ---- Output format ---- */
  { OV7670_REG_TSLB,              0x04u }, /* TSLB: OV output timing           */
  { OV7670_REG_COM7,              0x14u }, /* COM7: QVGA + RGB565             */
  { OV7670_REG_COM3,              0x0Cu }, /* COM3: DCW + down-sampling       */
  { OV7670_REG_COM14,             0x18u }, /* COM14: DCW PCLK + manual scale  */
  { OV7670_REG_COM15,             0xD0u }, /* COM15: full range + RGB565      */
  { OV7670_REG_RGB444,            0x00u }, /* RGB444: disable                 */

  /* ---- VGA window (sensor readout area, OmniVision default) ---- */
  { 0x17u, 0x13u }, /* HSTART */
  { 0x18u, 0x01u }, /* HSTOP */
  { 0x32u, 0xB6u }, /* HREF */
  { 0x19u, 0x02u }, /* VSTART */
  { 0x1Au, 0x7Au }, /* VSTOP */
  { 0x03u, 0x0Au }, /* VREF */

  /* ---- Scaling (160x128 via DCW + Digital Zoom) ---- */
  { OV7670_REG_SCALING_DCWCTR,    0x11u }, /* DCWCTR: V/H by2                */
  { OV7670_REG_SCALING_XSC,       0x40u }, /* XSC: horizontal 0.5x (320->160)*/
  { OV7670_REG_SCALING_YSC,       0x3Cu }, /* YSC: vertical 0.533x (240->128)*/
  { OV7670_REG_SCALING_PCLK_DIV,  0xF1u }, /* PCLK_DIV: /2 (match DCW by2)  */
  { OV7670_REG_SCALING_PCLK_DELAY, 0x02u }, /* PCLK_DELAY                      */

  /* ---- Gamma curve (0x7A-0x89) ---- */
  { 0x7Au, 0x20u }, { 0x7Bu, 0x10u }, { 0x7Cu, 0x1Eu }, { 0x7Du, 0x35u },
  { 0x7Eu, 0x5Au }, { 0x7Fu, 0x69u }, { 0x80u, 0x76u }, { 0x81u, 0x80u },
  { 0x82u, 0x88u }, { 0x83u, 0x8Fu }, { 0x84u, 0x96u }, { 0x85u, 0xA3u },
  { 0x86u, 0xAFu }, { 0x87u, 0xC4u }, { 0x88u, 0xD7u }, { 0x89u, 0xE8u },

  /* ---- COM8: disable AGC/AEC/AWB for parameter setup ---- */
  { OV7670_REG_COM8,              0xE0u }, /* FASTAEC+AECSTEP+BFILT only     */

  /* ---- AGC/AEC parameters ---- */
  { 0x00u, 0x00u }, /* GAIN */
  { 0x10u, 0x00u }, /* AECH */
  { 0x0Du, 0x40u }, /* COM4: magic reserved bit       */
  { OV7670_REG_COM9,              0x38u }, /* COM9: 16x gain ceiling         */
  { 0xA5u, 0x05u }, /* BD50MAX: 50Hz banding         */
  { 0xABu, 0x07u }, /* BD60MAX: 60Hz banding         */
  { 0x24u, 0x95u }, /* AEW: AGC upper limit           */
  { 0x25u, 0x33u }, /* AEB: AGC lower limit           */
  { 0x26u, 0xE3u }, /* VPT: AGC/AEC fast op region    */
  { 0x9Fu, 0x78u }, /* HAECC1                         */
  { 0xA0u, 0x68u }, /* HAECC2                         */
  { 0xA1u, 0x03u }, /* magic register                 */
  { 0xA6u, 0xD8u }, /* HAECC3                         */
  { 0xA7u, 0xD8u }, /* HAECC4                         */
  { 0xA8u, 0xF0u }, /* HAECC5                         */
  { 0xA9u, 0x90u }, /* HAECC6                         */
  { 0xAAu, 0x94u }, /* HAECC7                         */

  /* ---- COM8: re-enable AGC+AEC ---- */
  { OV7670_REG_COM8,              0xE5u }, /* FASTAEC+AECSTEP+BFILT+AGC+AEC */

  /* ---- Reserved / magic registers ---- */
  { 0x0Eu, 0x61u }, /* COM5 */
  { 0x0Fu, 0x4Bu }, /* COM6 */
  { 0x16u, 0x02u },
  { OV7670_REG_MVFP,              0x07u }, /* MVFP: mirror + vflip           */
  { 0x21u, 0x02u }, /* ADCCTR1 */
  { 0x22u, 0x91u }, /* ADCCTR2 */
  { 0x29u, 0x07u },
  { 0x33u, 0x0Bu },
  { 0x35u, 0x0Bu },
  { 0x37u, 0x1Du },
  { 0x38u, 0x71u },
  { 0x39u, 0x2Au },
  { 0x3Cu, 0x78u }, /* COM12 */
  { 0x4Du, 0x40u },
  { 0x4Eu, 0x20u },
  { 0x69u, 0x00u }, /* GFIX */
  { 0x6Bu, 0x4Au }, /* DBLV: PLL x4 */
  { 0x74u, 0x10u },
  { 0x8Du, 0x4Fu },
  { 0x8Eu, 0x00u },
  { 0x8Fu, 0x00u },
  { 0x90u, 0x00u },
  { 0x91u, 0x00u },
  { 0x92u, 0x19u }, /* LCC1 (from bayernfan) */
  { 0x96u, 0x00u },
  { 0x9Au, 0x00u },
  { 0xB0u, 0x84u },
  { 0xB1u, 0x0Cu },
  { 0xB2u, 0x0Eu },
  { 0xB3u, 0x82u },
  { 0xB8u, 0x0Au },

  /* ---- White balance parameters ---- */
  { 0x43u, 0x0Au },
  { 0x44u, 0xF0u },
  { 0x45u, 0x34u },
  { 0x46u, 0x58u },
  { 0x47u, 0x28u },
  { 0x48u, 0x3Au },
  { 0x59u, 0x88u },
  { 0x5Au, 0x88u },
  { 0x5Bu, 0x44u },
  { 0x5Cu, 0x67u },
  { 0x5Du, 0x49u },
  { 0x5Eu, 0x0Eu },
  { 0x6Cu, 0x0Au },
  { 0x6Du, 0x55u },
  { 0x6Eu, 0x11u },
  { 0x6Fu, 0x9Fu },
  { 0x6Au, 0x40u }, /* GGAIN */
  { 0x01u, 0x40u }, /* BLUE gain */
  { 0x02u, 0x60u }, /* RED gain */

  /* ---- COM8: re-enable AWB (all auto features on) ---- */
  { OV7670_REG_COM8,              0xE7u }, /* FASTAEC+AECSTEP+BFILT+AGC+AWB+AEC */

  /* ---- Color matrix (RGB565 specific) ---- */
  { 0x4Fu, 0xB3u }, /* MTX1 */
  { 0x50u, 0xB3u }, /* MTX2 */
  { 0x51u, 0x00u }, /* MTX3 */
  { 0x52u, 0x3Du }, /* MTX4 */
  { 0x53u, 0xA7u }, /* MTX5 */
  { 0x54u, 0xE4u }, /* MTX6 */
  { 0x58u, 0x9Eu }, /* MTXS: sign bits */

  /* ---- Post-processing ---- */
  { 0x41u, 0x08u }, /* COM16: AWB gain enable */
  { 0x3Fu, 0x00u }, /* EDGE: edge enhancement off */
  { 0x75u, 0x05u },
  { 0x76u, 0xE1u },
  { 0x4Cu, 0x00u }, /* DNSTH */
  { 0x77u, 0x01u },
  { OV7670_REG_COM13,             0xC0u }, /* COM13: gamma + UVSAT           */
  { 0x4Bu, 0x09u },
  { 0xC9u, 0x60u }, /* SATCR */
  { 0x56u, 0x40u }, /* CONTRAS */
  { 0x55u, 0x00u }, /* BRIGHT */
  { 0x41u, 0x38u }, /* COM16: final config */

  /* ---- Frame rate / noise ---- */
  { 0x34u, 0x11u }, /* ARBLM */
  { OV7670_REG_COM11,             0x0Au }, /* COM11: exp + auto 50/60Hz      */
  { 0xA4u, 0x88u }, /* NT_CTRL */
  { 0x96u, 0x00u },
  { 0x97u, 0x30u },
  { 0x98u, 0x20u },
  { 0x99u, 0x30u },
  { 0x9Au, 0x84u },
  { 0x9Bu, 0x29u },
  { 0x9Cu, 0x03u },
  { 0x9Du, 0x4Cu },
  { 0x9Eu, 0x3Fu },
  { 0x78u, 0x04u },

  /* ---- Trigger sequence (0x79/0xC8 pairs, OV chip init) ---- */
  { 0x79u, 0x01u }, { 0xC8u, 0xF0u },
  { 0x79u, 0x0Fu }, { 0xC8u, 0x00u },
  { 0x79u, 0x10u }, { 0xC8u, 0x7Eu },
  { 0x79u, 0x0Au }, { 0xC8u, 0x80u },
  { 0x79u, 0x0Bu }, { 0xC8u, 0x01u },
  { 0x79u, 0x0Cu }, { 0xC8u, 0x0Fu },
  { 0x79u, 0x0Du }, { 0xC8u, 0x20u },
  { 0x79u, 0x09u }, { 0xC8u, 0x80u },
  { 0x79u, 0x02u }, { 0xC8u, 0xC0u },
  { 0x79u, 0x03u }, { 0xC8u, 0x40u },
  { 0x79u, 0x05u }, { 0xC8u, 0x30u },
  { 0x79u, 0x26u },
};

bool OV7670_Init(void)
{
  /* Hardware power-up sequence */
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

  /* Software reset (COM7 RESET) */
  SCCB_WriteReg(OV7670_REG_COM7, 0x80u);
  DWT_DelayMs(5u);

  /* Write all registers */
  for (uint16_t i = 0u; i < sizeof(s_reg_config) / sizeof(s_reg_config[0]); i++)
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
