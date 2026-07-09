/**
  * @file    ov7670.c
  * @brief   OV7670 camera register configuration driver implementation
  */
#include "ov7670.h"
#include "ov7670_sccb.h"
#include "dwt_delay.h"
#include "stm32f1xx_hal.h"

/* OV7670 register addresses (based on Linux kernel ov7670.c + OmniVision datasheet) */

/* ---- Sensor controls ---- */
#define OV7670_REG_GAIN          0x00u
#define OV7670_REG_BLUE          0x01u
#define OV7670_REG_RED           0x02u
#define OV7670_REG_VREF          0x03u

/* ---- Common registers (COM1-COM16) ---- */
#define OV7670_REG_COM3          0x0Cu
#define OV7670_REG_COM4          0x0Du
#define OV7670_REG_COM5          0x0Eu
#define OV7670_REG_COM6          0x0Fu
#define OV7670_REG_AECH          0x10u
#define OV7670_REG_CLKRC         0x11u
#define OV7670_REG_COM7          0x12u
#define OV7670_REG_COM8          0x13u
#define OV7670_REG_COM9          0x14u
#define OV7670_REG_MVFP          0x1Eu
#define OV7670_REG_TSLB          0x3Au
#define OV7670_REG_COM11         0x3Bu
#define OV7670_REG_COM12         0x3Cu
#define OV7670_REG_COM13         0x3Du
#define OV7670_REG_COM14         0x3Eu
#define OV7670_REG_EDGE          0x3Fu
#define OV7670_REG_COM15         0x40u
#define OV7670_REG_COM16         0x41u

/* ---- Window / timing ---- */
#define OV7670_REG_HSTART        0x17u
#define OV7670_REG_HSTOP         0x18u
#define OV7670_REG_VSTART        0x19u
#define OV7670_REG_VSTOP         0x1Au
#define OV7670_REG_HREF          0x32u

/* ---- ADC control ---- */
#define OV7670_REG_ADCCTR1       0x21u
#define OV7670_REG_ADCCTR2       0x22u
#define OV7670_REG_CHLF          0x33u
#define OV7670_REG_ADC           0x37u
#define OV7670_REG_ACOM          0x38u
#define OV7670_REG_OFON          0x39u

/* ---- AGC / AEC ---- */
#define OV7670_REG_AEW           0x24u
#define OV7670_REG_AEB           0x25u
#define OV7670_REG_VPT           0x26u
#define OV7670_REG_HAECC1        0x9Fu
#define OV7670_REG_HAECC2        0xA0u
#define OV7670_REG_HAECC3        0xA6u
#define OV7670_REG_HAECC4        0xA7u
#define OV7670_REG_HAECC5        0xA8u
#define OV7670_REG_HAECC6        0xA9u
#define OV7670_REG_HAECC7        0xAAu
#define OV7670_REG_BD50MAX       0xA5u
#define OV7670_REG_BD60MAX       0xABu

/* ---- Color matrix ---- */
#define OV7670_REG_MTX1          0x4Fu
#define OV7670_REG_MTX2          0x50u
#define OV7670_REG_MTX3          0x51u
#define OV7670_REG_MTX4          0x52u
#define OV7670_REG_MTX5          0x53u
#define OV7670_REG_MTX6          0x54u
#define OV7670_REG_MTXS          0x58u

/* ---- Scaling ---- */
#define OV7670_REG_SCALING_XSC   0x70u
#define OV7670_REG_SCALING_YSC   0x71u
#define OV7670_REG_SCALING_DCWCTR 0x72u
#define OV7670_REG_SCALING_PCLK_DIV  0x73u
#define OV7670_REG_SCALING_PCLK_DELAY 0xA2u

/* ---- Gamma curve ---- */
#define OV7670_REG_GAM0          0x7Au
#define OV7670_REG_GAM1          0x7Bu
#define OV7670_REG_GAM2          0x7Cu
#define OV7670_REG_GAM3          0x7Du
#define OV7670_REG_GAM4          0x7Eu
#define OV7670_REG_GAM5          0x7Fu
#define OV7670_REG_GAM6          0x80u
#define OV7670_REG_GAM7          0x81u
#define OV7670_REG_GAM8          0x82u
#define OV7670_REG_GAM9          0x83u
#define OV7670_REG_GAM10         0x84u
#define OV7670_REG_GAM11         0x85u
#define OV7670_REG_GAM12         0x86u
#define OV7670_REG_GAM13         0x87u
#define OV7670_REG_GAM14         0x88u
#define OV7670_REG_GAM15         0x89u

/* ---- Pixel correction / denoise ---- */
#define OV7670_REG_REG76         0x76u
#define OV7670_REG_DNSTH         0x4Cu

/* ---- PLL / clock ---- */
#define OV7670_REG_DBLV          0x6Bu
#define OV7670_REG_GFIX          0x69u
#define OV7670_REG_ARBLM         0x34u

/* ---- Display controls ---- */
#define OV7670_REG_BRIGHT        0x55u
#define OV7670_REG_CONTRAS       0x56u
#define OV7670_REG_SATCR         0xC9u
#define OV7670_REG_RGB444        0x8Cu

/* ---- White balance ---- */
#define OV7670_REG_AWB0         0x43u
#define OV7670_REG_AWB1         0x44u
#define OV7670_REG_AWB2         0x45u
#define OV7670_REG_AWB3         0x46u
#define OV7670_REG_AWB4         0x47u
#define OV7670_REG_AWB5         0x48u
#define OV7670_REG_AWB_CTRL0    0x59u
#define OV7670_REG_AWB_CTRL1    0x5Au
#define OV7670_REG_AWB_CTRL2    0x5Bu
#define OV7670_REG_AWB_CTRL3    0x5Cu
#define OV7670_REG_AWB_CTRL4    0x5Du
#define OV7670_REG_AWB_CTRL5    0x5Eu
#define OV7670_REG_AWB_CTRL6    0x6Cu
#define OV7670_REG_AWB_CTRL7    0x6Du
#define OV7670_REG_AWB_CTRL8    0x6Eu
#define OV7670_REG_AWB_CTRL9    0x6Fu
#define OV7670_REG_GGAIN         0x6Au

/* ---- Frame / noise ---- */
#define OV7670_REG_NT_CTRL       0xA4u

/* ---- Lens correction ---- */
#define OV7670_REG_LCC1          0x92u

/* ---- Multiplexor sequence ---- */
#define OV7670_REG_MUX_SEL       0x79u
#define OV7670_REG_MUX_DAT       0xC8u

/* ---- Reserved / magic registers ---- */
#define OV7670_REG_MAGIC_16      0x16u
#define OV7670_REG_MAGIC_29      0x29u
#define OV7670_REG_MAGIC_35      0x35u
#define OV7670_REG_MAGIC_4B      0x4Bu
#define OV7670_REG_MAGIC_4D      0x4Du
#define OV7670_REG_MAGIC_4E      0x4Eu
#define OV7670_REG_MAGIC_74      0x74u
#define OV7670_REG_MAGIC_75      0x75u
#define OV7670_REG_MAGIC_77      0x77u
#define OV7670_REG_MAGIC_78      0x78u
#define OV7670_REG_MAGIC_8D      0x8Du
#define OV7670_REG_MAGIC_8E      0x8Eu
#define OV7670_REG_MAGIC_8F      0x8Fu
#define OV7670_REG_MAGIC_90      0x90u
#define OV7670_REG_MAGIC_91      0x91u
#define OV7670_REG_MAGIC_96      0x96u
#define OV7670_REG_MAGIC_97      0x97u
#define OV7670_REG_MAGIC_98      0x98u
#define OV7670_REG_MAGIC_99      0x99u
#define OV7670_REG_MAGIC_9A      0x9Au
#define OV7670_REG_MAGIC_9B      0x9Bu
#define OV7670_REG_MAGIC_9C      0x9Cu
#define OV7670_REG_MAGIC_9D      0x9Du
#define OV7670_REG_MAGIC_9E      0x9Eu
#define OV7670_REG_MAGIC_A1      0xA1u
#define OV7670_REG_MAGIC_B0      0xB0u
#define OV7670_REG_MAGIC_B1      0xB1u
#define OV7670_REG_MAGIC_B2      0xB2u
#define OV7670_REG_MAGIC_B3      0xB3u
#define OV7670_REG_MAGIC_B8      0xB8u

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
  { OV7670_REG_TSLB,   0x04u }, /* TSLB: OV output timing           */
  { OV7670_REG_COM7,   0x14u }, /* COM7: QVGA + RGB565             */
  { OV7670_REG_COM3,   0x0Cu }, /* COM3: DCW + down-sampling       */
  { OV7670_REG_COM14,  0x18u }, /* COM14: DCW PCLK + manual scale  */
  { OV7670_REG_COM15,  0xD0u }, /* COM15: full range + RGB565      */
  { OV7670_REG_RGB444, 0x00u }, /* RGB444: disable                 */

  /* ---- VGA window (sensor readout area, OmniVision default) ---- */
  { OV7670_REG_HSTART, 0x13u },
  { OV7670_REG_HSTOP,  0x01u },
  { OV7670_REG_HREF,   0xB6u },
  { OV7670_REG_VSTART, 0x02u },
  { OV7670_REG_VSTOP,  0x7Au },
  { OV7670_REG_VREF,   0x0Au },

  /* ---- Scaling (160x128 via DCW + Digital Zoom) ---- */
  { OV7670_REG_SCALING_DCWCTR,     0x11u }, /* DCWCTR: V/H by2                */
  { OV7670_REG_SCALING_XSC,        0x40u }, /* XSC: horizontal 0.5x (320->160)*/
  { OV7670_REG_SCALING_YSC,        0x3Cu }, /* YSC: vertical 0.533x (240->128)*/
  { OV7670_REG_SCALING_PCLK_DIV,   0xF1u }, /* PCLK_DIV: /2 (match DCW by2)  */
  { OV7670_REG_SCALING_PCLK_DELAY, 0x02u }, /* PCLK_DELAY                      */

  /* ---- Gamma curve (0x7A-0x89) ---- */
  { OV7670_REG_GAM0,  0x20u },
  { OV7670_REG_GAM1,  0x10u },
  { OV7670_REG_GAM2,  0x1Eu },
  { OV7670_REG_GAM3,  0x35u },
  { OV7670_REG_GAM4,  0x5Au },
  { OV7670_REG_GAM5,  0x69u },
  { OV7670_REG_GAM6,  0x76u },
  { OV7670_REG_GAM7,  0x80u },
  { OV7670_REG_GAM8,  0x88u },
  { OV7670_REG_GAM9,  0x8Fu },
  { OV7670_REG_GAM10, 0x96u },
  { OV7670_REG_GAM11, 0xA3u },
  { OV7670_REG_GAM12, 0xAFu },
  { OV7670_REG_GAM13, 0xC4u },
  { OV7670_REG_GAM14, 0xD7u },
  { OV7670_REG_GAM15, 0xE8u },

  /* ---- COM8: disable AGC/AEC/AWB for parameter setup ---- */
  { OV7670_REG_COM8, 0xE0u }, /* FASTAEC+AECSTEP+BFILT only     */

  /* ---- AGC/AEC parameters ---- */
  { OV7670_REG_GAIN,     0x00u },
  { OV7670_REG_AECH,     0x00u },
  { OV7670_REG_COM4,     0x40u },
  { OV7670_REG_COM9,     0x38u },
  { OV7670_REG_BD50MAX,  0x05u },
  { OV7670_REG_BD60MAX,  0x07u },
  { OV7670_REG_AEW,      0x95u },
  { OV7670_REG_AEB,      0x33u },
  { OV7670_REG_VPT,      0xE3u },
  { OV7670_REG_HAECC1,   0x78u },
  { OV7670_REG_HAECC2,   0x68u },
  { OV7670_REG_MAGIC_A1, 0x03u },
  { OV7670_REG_HAECC3,   0xD8u },
  { OV7670_REG_HAECC4,   0xD8u },
  { OV7670_REG_HAECC5,   0xF0u },
  { OV7670_REG_HAECC6,   0x90u },
  { OV7670_REG_HAECC7,   0x94u },

  /* ---- COM8: re-enable AGC+AEC ---- */
  { OV7670_REG_COM8, 0xE5u }, /* FASTAEC+AECSTEP+BFILT+AGC+AEC */

  /* ---- Reserved / magic registers ---- */
  { OV7670_REG_COM5,     0x61u },
  { OV7670_REG_COM6,     0x4Bu },
  { OV7670_REG_MAGIC_16, 0x02u },
  { OV7670_REG_MVFP,     0x07u },
  { OV7670_REG_ADCCTR1,  0x02u },
  { OV7670_REG_ADCCTR2,  0x91u },
  { OV7670_REG_MAGIC_29, 0x07u },
  { OV7670_REG_CHLF,     0x0Bu },
  { OV7670_REG_MAGIC_35, 0x0Bu },
  { OV7670_REG_ADC,      0x1Du },
  { OV7670_REG_ACOM,     0x71u },
  { OV7670_REG_OFON,     0x2Au },
  { OV7670_REG_COM12,    0x78u },
  { OV7670_REG_MAGIC_4D, 0x40u },
  { OV7670_REG_MAGIC_4E, 0x20u },
  { OV7670_REG_GFIX,     0x00u },
  { OV7670_REG_DBLV,     0x4Au },
  { OV7670_REG_MAGIC_74, 0x10u },
  { OV7670_REG_MAGIC_8D, 0x4Fu },
  { OV7670_REG_MAGIC_8E, 0x00u },
  { OV7670_REG_MAGIC_8F, 0x00u },
  { OV7670_REG_MAGIC_90, 0x00u },
  { OV7670_REG_MAGIC_91, 0x00u },
  { OV7670_REG_LCC1,     0x19u },
  { OV7670_REG_MAGIC_96, 0x00u },
  { OV7670_REG_MAGIC_9A, 0x00u },
  { OV7670_REG_MAGIC_B0, 0x84u },
  { OV7670_REG_MAGIC_B1, 0x0Cu },
  { OV7670_REG_MAGIC_B2, 0x0Eu },
  { OV7670_REG_MAGIC_B3, 0x82u },
  { OV7670_REG_MAGIC_B8, 0x0Au },

  /* ---- White balance parameters ---- */
  { OV7670_REG_AWB0,      0x0Au },
  { OV7670_REG_AWB1,      0xF0u },
  { OV7670_REG_AWB2,      0x34u },
  { OV7670_REG_AWB3,      0x58u },
  { OV7670_REG_AWB4,      0x28u },
  { OV7670_REG_AWB5,      0x3Au },
  { OV7670_REG_AWB_CTRL0, 0x88u },
  { OV7670_REG_AWB_CTRL1, 0x88u },
  { OV7670_REG_AWB_CTRL2, 0x44u },
  { OV7670_REG_AWB_CTRL3, 0x67u },
  { OV7670_REG_AWB_CTRL4, 0x49u },
  { OV7670_REG_AWB_CTRL5, 0x0Eu },
  { OV7670_REG_AWB_CTRL6, 0x0Au },
  { OV7670_REG_AWB_CTRL7, 0x55u },
  { OV7670_REG_AWB_CTRL8, 0x11u },
  { OV7670_REG_AWB_CTRL9, 0x9Fu },
  { OV7670_REG_GGAIN,     0x40u },
  { OV7670_REG_BLUE,      0x40u },
  { OV7670_REG_RED,       0x60u },

  /* ---- COM8: re-enable AWB (all auto features on) ---- */
  { OV7670_REG_COM8, 0xE7u }, /* FASTAEC+AECSTEP+BFILT+AGC+AWB+AEC */

  /* ---- Color matrix (RGB565 specific) ---- */
  { OV7670_REG_MTX1, 0xB3u },
  { OV7670_REG_MTX2, 0xB3u },
  { OV7670_REG_MTX3, 0x00u },
  { OV7670_REG_MTX4, 0x3Du },
  { OV7670_REG_MTX5, 0xA7u },
  { OV7670_REG_MTX6, 0xE4u },
  { OV7670_REG_MTXS, 0x9Eu },

  /* ---- Post-processing ---- */
  { OV7670_REG_COM16,    0x08u },
  { OV7670_REG_EDGE,     0x00u },
  { OV7670_REG_MAGIC_75, 0x05u },
  { OV7670_REG_REG76,    0xE1u },
  { OV7670_REG_DNSTH,    0x00u },
  { OV7670_REG_MAGIC_77, 0x01u },
  { OV7670_REG_COM13,    0xC0u },
  { OV7670_REG_MAGIC_4B, 0x09u },
  { OV7670_REG_SATCR,    0x60u },
  { OV7670_REG_CONTRAS,  0x40u },
  { OV7670_REG_BRIGHT,   0x00u },
  { OV7670_REG_COM16,    0x38u },

  /* ---- Frame rate / noise ---- */
  { OV7670_REG_ARBLM,    0x11u },
  { OV7670_REG_COM11,    0x0Au },
  { OV7670_REG_NT_CTRL,  0x88u },
  { OV7670_REG_MAGIC_96, 0x00u },
  { OV7670_REG_MAGIC_97, 0x30u },
  { OV7670_REG_MAGIC_98, 0x20u },
  { OV7670_REG_MAGIC_99, 0x30u },
  { OV7670_REG_MAGIC_9A, 0x84u },
  { OV7670_REG_MAGIC_9B, 0x29u },
  { OV7670_REG_MAGIC_9C, 0x03u },
  { OV7670_REG_MAGIC_9D, 0x4Cu },
  { OV7670_REG_MAGIC_9E, 0x3Fu },
  { OV7670_REG_MAGIC_78, 0x04u },

  /* ---- Trigger sequence (MUX_SEL / MUX_DAT pairs, OV chip init) ---- */
  { OV7670_REG_MUX_SEL, 0x01u },
  { OV7670_REG_MUX_DAT, 0xF0u },
  { OV7670_REG_MUX_SEL, 0x0Fu },
  { OV7670_REG_MUX_DAT, 0x00u },
  { OV7670_REG_MUX_SEL, 0x10u },
  { OV7670_REG_MUX_DAT, 0x7Eu },
  { OV7670_REG_MUX_SEL, 0x0Au },
  { OV7670_REG_MUX_DAT, 0x80u },
  { OV7670_REG_MUX_SEL, 0x0Bu },
  { OV7670_REG_MUX_DAT, 0x01u },
  { OV7670_REG_MUX_SEL, 0x0Cu },
  { OV7670_REG_MUX_DAT, 0x0Fu },
  { OV7670_REG_MUX_SEL, 0x0Du },
  { OV7670_REG_MUX_DAT, 0x20u },
  { OV7670_REG_MUX_SEL, 0x09u },
  { OV7670_REG_MUX_DAT, 0x80u },
  { OV7670_REG_MUX_SEL, 0x02u },
  { OV7670_REG_MUX_DAT, 0xC0u },
  { OV7670_REG_MUX_SEL, 0x03u },
  { OV7670_REG_MUX_DAT, 0x40u },
  { OV7670_REG_MUX_SEL, 0x05u },
  { OV7670_REG_MUX_DAT, 0x30u },
  { OV7670_REG_MUX_SEL, 0x26u },
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
