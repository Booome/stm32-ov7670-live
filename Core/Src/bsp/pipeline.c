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

/* External handles (defined in main.c) */
extern TIM_HandleTypeDef htim3;
extern SPI_HandleTypeDef hspi2;
extern DMA_HandleTypeDef hdma_tim3_ch4_up;

/* VSYNC delay: 6ms. */
#define PIPELINE_VSYNC_DELAY_US  6000u

/* Module state */
static volatile Pipeline_StateTypeDef s_state = PIPELINE_STATE_DISABLED;
static volatile uint32_t s_bytes_sent;
static volatile bool s_abort_pending;
static volatile uint32_t s_frame_count;
static DWT_DelayHandle s_vsync_delay;

/* Frame buffer: 640 bytes, 2 x 320B ping-pong */
static uint8_t s_pipeline_buffer[PIPELINE_BUFFER_SIZE];

/* Forward declarations */
static void OnDmaHalfCplt(DMA_HandleTypeDef *hdma);
static void OnDmaCplt(DMA_HandleTypeDef *hdma);

/* ---- SPI TX DMA LL helpers (no HAL callbacks, no NVIC) ----
 *
 * SPI DMA (DMA1_Channel5) completes 320B in ~142us @ 18MHz SPI.
 * Camera DMA half-buffer takes ~222us @ 1.44MHz.  The SPI DMA always
 * finishes before the next Camera DMA callback, so we can busy-wait
 * in the ISR with near-zero overhead.
 *
 * All one-time setup is in Pipeline_Init; per-transfer restart is
 * in SpiTxDma_Start (~15 cycles, ISR-safe). */

/** @brief  Start SPI TX DMA with LL registers (ISR-safe, ~15 cycles) */
static inline void SpiTxDma_Start(uint8_t *buf, uint16_t len)
{
  DMA1->IFCR = DMA_IFCR_CGIF5;
  DMA1_Channel5->CMAR = (uint32_t)buf;
  DMA1_Channel5->CNDTR = (uint32_t)len;
  DMA1_Channel5->CCR |= DMA_CCR_EN;
}

/** @brief  Wait for SPI TX DMA to finish.
  *         Polls CNDTR (not CCR EN — EN is not reliably cleared by hw when
  *         TXDMAEN is permanently set).  Manually clears EN after CNDTR=0,
  *         then waits for SPI BSY to ensure the wire is idle. */
static inline void SpiTxDma_WaitDone(void)
{
  while (DMA1_Channel5->CNDTR > 0u)
  {
  }
  DMA1_Channel5->CCR &= ~DMA_CCR_EN;
  while (SPI2->SR & SPI_SR_BSY)
  {
  }
}

/** @brief  Restore SPI2 1-line bidirectional TX mode.
  *         HAL_SPI_Transmit (blocking, used by LCD_SetAddrWindow) clears
  *         BIDIMODE+BIDIOE+SPE; BIDIMODE/BIDIOE require SPE=0 to change
  *         (RM0008 §25.3.3).  Also clears leftover OVR. */
static inline void SpiRestore1LineTx(void)
{
  __HAL_SPI_DISABLE(&hspi2);
  SET_BIT(hspi2.Instance->CR1, SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE);
  __HAL_SPI_ENABLE(&hspi2);
  (void)SPI2->DR;
  (void)SPI2->SR;
}

/**
  * @brief   Start FIFO read pipeline
  */
static void ReadStart(void)
{
  if (s_abort_pending)
  {
    HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_4);
    HAL_DMA_Abort(&hdma_tim3_ch4_up);
    OV7670_FIFO_OE_High();
    LCD_CS_High();
    s_abort_pending = false;
  }

  /* Reset FIFO read pointer: AL422B requires RRST low + RCK falling edge */
  {
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = OV7670_FIFO_RCK_Pin;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    OV7670_FIFO_RCK_GPIO_Port->BRR = OV7670_FIFO_RCK_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(OV7670_FIFO_RCK_GPIO_Port, &gpio);

    HAL_GPIO_WritePin(OV7670_FIFO_RCK_GPIO_Port, OV7670_FIFO_RCK_Pin, GPIO_PIN_SET);
    OV7670_FIFO_RRST_Low();
    HAL_GPIO_WritePin(OV7670_FIFO_RCK_GPIO_Port, OV7670_FIFO_RCK_Pin, GPIO_PIN_RESET);
    OV7670_FIFO_RRST_High();

    OV7670_FIFO_RCK_GPIO_Port->BRR = OV7670_FIFO_RCK_Pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(OV7670_FIFO_RCK_GPIO_Port, &gpio);
  }

  OV7670_FIFO_OE_Low();

  LCD_SetAddrWindow(0u, 0u, PIPELINE_WIDTH - 1u, PIPELINE_HEIGHT - 1u);

  /* LCD_SetAddrWindow used blocking HAL_SPI_Transmit: restore 1-line TX */
  SpiRestore1LineTx();

  s_bytes_sent = 0u;

  hdma_tim3_ch4_up.XferHalfCpltCallback = OnDmaHalfCplt;
  hdma_tim3_ch4_up.XferCpltCallback = OnDmaCplt;
  HAL_DMA_Start_IT(&hdma_tim3_ch4_up,
                    (uint32_t)OV7670_DATA_ADDR,
                    (uint32_t)s_pipeline_buffer,
                    PIPELINE_BUFFER_SIZE);

  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

  s_state = PIPELINE_STATE_FRAME_CAPTURING;
}

static void FrameDone(void)
{
  SpiTxDma_WaitDone();

  HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_4);
  HAL_DMA_Abort(&hdma_tim3_ch4_up);

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

  /* Pre-configure SPI2 TX DMA (DMA1_Channel5) for LL operation.
   *
   * CPAR: HAL_DMA_Init (CubeMX) does NOT set CPAR.  Must be set once.
   * CCR:  HAL_DMA_Init set DIR/MINC/PSIZE/MSIZE/MODE/PRIORITY.
   *       No interrupt enables (we poll).
   * CR2:  TXDMAEN + ERRIE permanently on (HAL clears on completion).
   * NVIC: off — we poll CCR EN + BSY. */

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

static void OnDmaHalfCplt(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  if (s_state != PIPELINE_STATE_FRAME_CAPTURING)
  {
    return;
  }

  SpiTxDma_WaitDone();
  SpiTxDma_Start(s_pipeline_buffer, PIPELINE_HALF_SIZE);
  s_bytes_sent += PIPELINE_HALF_SIZE;
}

static void OnDmaCplt(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
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

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin != OV7670_VSYNC_Pin)
  {
    return;
  }
  OnVsync();
}
