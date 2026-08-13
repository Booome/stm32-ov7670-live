/**
  * @file    pipeline.c
  * @brief   Dual-DMA frame capture pipeline implementation
  */
#include "pipeline.h"
#include "st7735.h"
#include "ov7670.h"
#include "dwt_delay.h"
#include "main.h"
#include "stm32f1xx_hal.h"

#define PIPELINE_READ_DELAY_US  15000u

static volatile Pipeline_StateTypeDef s_state = PIPELINE_STATE_DISABLED;
static volatile uint32_t s_bytes_sent;
static volatile bool s_read_pending;
static volatile uint32_t s_frame_count;
static DWT_DelayHandle s_vsync_delay;

static uint8_t s_pipeline_buffer[PIPELINE_BUFFER_SIZE];

static void OnDmaHalfCplt(void);
static void OnDmaCplt(void);

/* SPI DMA (320B ~142us) always finishes before the next camera half-buffer
 * (~222us), so busy-waiting in the ISR costs nothing. */
static inline void SpiTxDma_Start(uint8_t *buf, uint16_t len)
{
  PIPELINE_LCD_DMA->IFCR = PIPELINE_LCD_DMA_CLEAR;
  PIPELINE_LCD_DMA_CHNL->CMAR = (uint32_t)buf;
  PIPELINE_LCD_DMA_CHNL->CNDTR = (uint32_t)len;
  PIPELINE_LCD_DMA_CHNL->CCR |= DMA_CCR_EN;
}

static inline void SpiTxDma_WaitDone(void)
{
  /* Poll CNDTR, not CCR EN: EN is not reliably cleared with TXDMAEN set. */
  while (PIPELINE_LCD_DMA_CHNL->CNDTR > 0u);
  PIPELINE_LCD_DMA_CHNL->CCR &= ~DMA_CCR_EN;
  while (PIPELINE_LCD_SPI->SR & SPI_SR_BSY);
}

static inline void SpiRestore1LineTx(void)
{
  /* LCD_SetAddrWindow's blocking HAL_SPI_Transmit clears BIDIMODE+BIDIOE. */
  CLEAR_BIT(PIPELINE_LCD_SPI->CR1, SPI_CR1_SPE);
  SET_BIT(PIPELINE_LCD_SPI->CR1, SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE);
  SET_BIT(PIPELINE_LCD_SPI->CR1, SPI_CR1_SPE);
  (void)PIPELINE_LCD_SPI->DR;        /* clear leftover OVR */
  (void)PIPELINE_LCD_SPI->SR;
}

static inline void CamDma_Start(void)
{
  PIPELINE_CAM_DMA_CHNL->CCR &= ~DMA_CCR_EN;
  PIPELINE_CAM_DMA->IFCR = PIPELINE_CAM_DMA_CLEAR;
  PIPELINE_CAM_DMA_CHNL->CNDTR = PIPELINE_BUFFER_SIZE;
  PIPELINE_CAM_DMA_CHNL->CPAR = (uint32_t)OV7670_DATA_ADDR;
  PIPELINE_CAM_DMA_CHNL->CMAR = (uint32_t)s_pipeline_buffer;
  PIPELINE_CAM_DMA_CHNL->CCR |= DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE;
  PIPELINE_CAM_DMA_CHNL->CCR |= DMA_CCR_EN;
}

static inline void CamDma_Stop(void)
{
  PIPELINE_CAM_DMA_CHNL->CCR &= ~(DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE | DMA_CCR_EN);
  PIPELINE_CAM_DMA->IFCR = PIPELINE_CAM_DMA_CLEAR;
}

static inline void TimPwmStart(void)
{
  /* Clear CNT so CC4E enable sees CNT<CCR4, avoiding a stray RCK edge */
  PIPELINE_CAM_TIM->CNT = 0u;
  PIPELINE_CAM_TIM->CCER |= TIM_CCER_CC4E;
  PIPELINE_CAM_TIM->CR1 |= TIM_CR1_CEN;
}

static inline void TimPwmStop(void)
{
  PIPELINE_CAM_TIM->CCER &= ~TIM_CCER_CC4E;
  PIPELINE_CAM_TIM->CR1 &= ~TIM_CR1_CEN;
}

static void ReadStart(void)
{
  /* Reset FIFO read pointer: pulse RCK low->high->low while RRST is low.
   * BRR pre-clears ODR so the AF<->GPIO mode switches never glitch. */
  {
    GPIO_TypeDef *port = OV7670_FIFO_RCK_GPIO_Port;
    const uint32_t pin = OV7670_FIFO_RCK_Pin;

    port->BRR = pin;
    MODIFY_REG(port->CRL, PIPELINE_RCK_CRL_MASK, PIPELINE_RCK_CRL_OUTPUT_PP);

    OV7670_FIFO_RRST_Low();
    port->BSRR = pin;                 /* RCK high */
    port->BRR = pin;                  /* RCK falling edge, RRST low */
    OV7670_FIFO_RRST_High();

    port->BRR = pin;
    MODIFY_REG(port->CRL, PIPELINE_RCK_CRL_MASK, PIPELINE_RCK_CRL_AF_PP);
  }

  OV7670_FIFO_OE_Low();

  LCD_SetAddrWindow(0u, 0u, PIPELINE_WIDTH - 1u, PIPELINE_HEIGHT - 1u);

  SpiRestore1LineTx();

  s_bytes_sent = 0u;

  CamDma_Start();

  TimPwmStart();

  s_state = PIPELINE_STATE_FRAME_CAPTURING;
}

static void FrameDone(void)
{
  SpiTxDma_WaitDone();

  TimPwmStop();
  CamDma_Stop();

  OV7670_FIFO_OE_High();
  LCD_CS_High();

  s_frame_count++;
  s_state = PIPELINE_STATE_IDLE;
}

void Pipeline_Init(void)
{
  s_state = PIPELINE_STATE_IDLE;
  s_bytes_sent = 0u;
  s_frame_count = 0u;
  s_read_pending = false;

  /* HAL_DMA_Init does not set CPAR; set once here. */
  PIPELINE_LCD_DMA_CHNL->CPAR = (uint32_t)&PIPELINE_LCD_SPI->DR;

  SpiRestore1LineTx();

  SET_BIT(PIPELINE_LCD_SPI->CR2, SPI_CR2_TXDMAEN | SPI_CR2_ERRIE);
  HAL_NVIC_DisableIRQ(PIPELINE_LCD_DMA_IRQN);
}

Pipeline_StateTypeDef Pipeline_GetState(void)
{
  return s_state;
}

uint32_t Pipeline_GetFrameCount(void)
{
  return s_frame_count;
}

void Pipeline_Poll(void)
{
  if (s_state == PIPELINE_STATE_FRAME_DONE)
  {
    FrameDone();
  }

  if (s_read_pending && (s_state == PIPELINE_STATE_IDLE) &&
      DWT_DelayExpired(&s_vsync_delay))
  {
    ReadStart();
    s_read_pending = false;
  }
}

static void OnVsync(void)
{
  OV7670_FIFO_WRST_Low();
  DWT_DelayUs(1u);
  OV7670_FIFO_WRST_High();
  OV7670_FIFO_WR_High();

  DWT_DelayStart(&s_vsync_delay, PIPELINE_READ_DELAY_US);
  s_read_pending = true;
}

void Pipeline_CameraDmaIrq(void)
{
  uint32_t isr = PIPELINE_CAM_DMA->ISR;

  if ((isr & PIPELINE_CAM_DMA_HTIF) != 0u)
  {
    PIPELINE_CAM_DMA->IFCR = PIPELINE_CAM_DMA_CLR_HTIF;
    OnDmaHalfCplt();
  }
  else if ((isr & PIPELINE_CAM_DMA_TCIF) != 0u)
  {
    PIPELINE_CAM_DMA->IFCR = PIPELINE_CAM_DMA_CLR_TCIF;
    OnDmaCplt();
  }
}

static void OnDmaHalfCplt(void)
{
  if (s_state != PIPELINE_STATE_FRAME_CAPTURING)
  {
    return;
  }

  SpiTxDma_WaitDone();
  SpiTxDma_Start(s_pipeline_buffer, PIPELINE_HALF_SIZE);
  s_bytes_sent += PIPELINE_HALF_SIZE;
}

static void OnDmaCplt(void)
{
  if (s_state != PIPELINE_STATE_FRAME_CAPTURING)
  {
    return;
  }

  SpiTxDma_WaitDone();
  SpiTxDma_Start(&s_pipeline_buffer[PIPELINE_HALF_SIZE], PIPELINE_HALF_SIZE);
  s_bytes_sent += PIPELINE_HALF_SIZE;

  if (s_bytes_sent >= PIPELINE_FRAME_SIZE)
  {
    s_state = PIPELINE_STATE_FRAME_DONE;
  }
}

void Pipeline_VsyncExtiIrq(void)
{
  if ((PIPELINE_EXTI->PR & OV7670_VSYNC_Pin) != 0u)
  {
    PIPELINE_EXTI->PR = OV7670_VSYNC_Pin;
    OnVsync();
  }
}
