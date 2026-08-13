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

extern SPI_HandleTypeDef hspi2;

#define PIPELINE_VSYNC_DELAY_US  6000u

static volatile Pipeline_StateTypeDef s_state = PIPELINE_STATE_DISABLED;
static volatile uint32_t s_bytes_sent;
static volatile bool s_abort_pending;
static volatile uint32_t s_frame_count;
static DWT_DelayHandle s_vsync_delay;

static uint8_t s_pipeline_buffer[PIPELINE_BUFFER_SIZE];

static void OnDmaHalfCplt(void);
static void OnDmaCplt(void);

/* SPI DMA (320B ~142us) always finishes before the next camera half-buffer
 * (~222us), so busy-waiting in the ISR costs nothing. */
static inline void SpiTxDma_Start(uint8_t *buf, uint16_t len)
{
  DMA1->IFCR = DMA_IFCR_CGIF5;
  DMA1_Channel5->CMAR = (uint32_t)buf;
  DMA1_Channel5->CNDTR = (uint32_t)len;
  DMA1_Channel5->CCR |= DMA_CCR_EN;
}

static inline void SpiTxDma_WaitDone(void)
{
  /* Poll CNDTR, not CCR EN: EN is not reliably cleared with TXDMAEN set. */
  while (DMA1_Channel5->CNDTR > 0u)
  {
  }
  DMA1_Channel5->CCR &= ~DMA_CCR_EN;
  while (SPI2->SR & SPI_SR_BSY)
  {
  }
}

static inline void SpiRestore1LineTx(void)
{
  /* LCD_SetAddrWindow's blocking HAL_SPI_Transmit clears BIDIMODE+BIDIOE. */
  __HAL_SPI_DISABLE(&hspi2);
  SET_BIT(hspi2.Instance->CR1, SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE);
  __HAL_SPI_ENABLE(&hspi2);
  (void)SPI2->DR;                    /* clear leftover OVR */
  (void)SPI2->SR;
}

static inline void CamDma_Start(void)
{
  DMA1_Channel3->CCR &= ~DMA_CCR_EN;
  DMA1->IFCR = DMA_IFCR_CGIF3;
  DMA1_Channel3->CNDTR = PIPELINE_BUFFER_SIZE;
  DMA1_Channel3->CPAR = (uint32_t)OV7670_DATA_ADDR;
  DMA1_Channel3->CMAR = (uint32_t)s_pipeline_buffer;
  DMA1_Channel3->CCR |= DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE;
  DMA1_Channel3->CCR |= DMA_CCR_EN;
}

static inline void CamDma_Stop(void)
{
  DMA1_Channel3->CCR &= ~(DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE | DMA_CCR_EN);
  DMA1->IFCR = DMA_IFCR_CGIF3;
}

static inline void TimPwmStart(void)
{
  TIM3->CCER |= TIM_CCER_CC4E;
  TIM3->CR1 |= TIM_CR1_CEN;
}

static inline void TimPwmStop(void)
{
  TIM3->CCER &= ~TIM_CCER_CC4E;
  TIM3->CR1 &= ~TIM_CR1_CEN;
}

/* RCK = PB1, CRL bits [7:4]. */
#define RCK_CRL_FIELD_MASK  (0x0Fu << 4u)
#define RCK_CRL_OUTPUT_PP   (0x03u << 4u)
#define RCK_CRL_AF_PP       (0x0Bu << 4u)

static void ReadStart(void)
{
  if (s_abort_pending)
  {
    TimPwmStop();
    CamDma_Stop();
    OV7670_FIFO_OE_High();
    LCD_CS_High();
    s_abort_pending = false;
  }

  /* Reset FIFO read pointer: RRST low + RCK falling edge.  BRR pre-clears
   * ODR so the AF<->GPIO mode switches never glitch. */
  {
    GPIO_TypeDef *port = OV7670_FIFO_RCK_GPIO_Port;
    const uint32_t pin = OV7670_FIFO_RCK_Pin;

    port->BRR = pin;
    MODIFY_REG(port->CRL, RCK_CRL_FIELD_MASK, RCK_CRL_OUTPUT_PP);

    port->BSRR = pin;                 /* RCK high */
    OV7670_FIFO_RRST_Low();
    port->BRR = pin;                  /* RCK falling edge, RRST low */
    OV7670_FIFO_RRST_High();

    port->BRR = pin;
    MODIFY_REG(port->CRL, RCK_CRL_FIELD_MASK, RCK_CRL_AF_PP);
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
  OV7670_FIFO_WR_Low();

  s_frame_count++;
  s_state = PIPELINE_STATE_IDLE;
}

void Pipeline_Init(void)
{
  s_state = PIPELINE_STATE_IDLE;
  s_bytes_sent = 0u;
  s_frame_count = 0u;

  /* HAL_DMA_Init does not set CPAR; set once here. */
  DMA1_Channel5->CPAR = (uint32_t)&hspi2.Instance->DR;

  SpiRestore1LineTx();

  SET_BIT(hspi2.Instance->CR2, SPI_CR2_TXDMAEN | SPI_CR2_ERRIE);
  HAL_NVIC_DisableIRQ(DMA1_Channel5_IRQn);
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
  if (s_state == PIPELINE_STATE_FRAME_START)
  {
    if (DWT_DelayExpired(&s_vsync_delay))
    {
      ReadStart();
    }
  }
  else if (s_state == PIPELINE_STATE_FRAME_DONE)
  {
    FrameDone();
  }
}

static void OnVsync(void)
{
  OV7670_FIFO_WRST_Low();
  DWT_DelayUs(1u);
  OV7670_FIFO_WRST_High();
  OV7670_FIFO_WR_High();

  s_abort_pending = (s_state != PIPELINE_STATE_IDLE);

  s_state = PIPELINE_STATE_FRAME_START;
  DWT_DelayStart(&s_vsync_delay, PIPELINE_VSYNC_DELAY_US);
}

void Pipeline_CameraDmaIrq(void)
{
  uint32_t isr = DMA1->ISR;

  if ((isr & DMA_ISR_HTIF3) != 0u)
  {
    DMA1->IFCR = DMA_IFCR_CHTIF3;
    OnDmaHalfCplt();
  }
  else if ((isr & DMA_ISR_TCIF3) != 0u)
  {
    DMA1->IFCR = DMA_IFCR_CTCIF3;
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
  if ((EXTI->PR & OV7670_VSYNC_Pin) != 0u)
  {
    EXTI->PR = OV7670_VSYNC_Pin;
    OnVsync();
  }
}
