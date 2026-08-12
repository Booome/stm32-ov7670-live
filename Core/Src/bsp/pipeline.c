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

/* VSYNC delay: 6ms.  Avg write rate is 1.23MB/s (320B/260us row), read rate
 * is 1.44MB/s.  Delaying read start by 6ms gives the write pointer a lead of
 * 6ms * 1.23MB/s = 7.4KB, which the read (0.21MB/s faster) cannot close within
 * the 28.4ms frame read (needs ~35ms), so the read never overtakes the write.
 * Read completes at 6+28.4 = 34.4ms, just inside the 34.8ms frame period, so
 * every VSYNC can start a fresh frame read (no dropped frames -> ~28.7fps).
 * Bounds: delay must be > 4.86ms (write lead must survive the whole frame
 * read) and <= 6.36ms (read must finish within one frame period). */
#define PIPELINE_VSYNC_DELAY_US  6000u

/* Module state */
static volatile Pipeline_StateTypeDef s_state = PIPELINE_STATE_DISABLED;
static volatile uint32_t s_bytes_sent;
static volatile bool s_spi_dma_busy;
static volatile bool s_abort_pending;
static volatile uint32_t s_frame_count;
static DWT_DelayHandle s_vsync_delay;

/* Frame buffer: 640 bytes, 2 x 320B ping-pong */
static uint8_t s_pipeline_buffer[PIPELINE_BUFFER_SIZE];

/* Forward declarations for DMA callback wrappers */
static void DmaHalfCpltCb(DMA_HandleTypeDef *hdma);
static void DmaCpltCb(DMA_HandleTypeDef *hdma);

/**
  * @brief   Start FIFO read pipeline
  *
  *          Called after VSYNC delay expires. Sets up FIFO read pointer,
  *          LCD address window, then starts TIM3 PWM + Camera DMA.
  */
static void ReadStart(void)
{
  /* Guard: if the previous frame's DMA/PWM is still running because VSYNC
   * preempted FrameDone, stop it before starting the new frame. */
  if (s_abort_pending)
  {
    HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_4);
    HAL_DMA_Abort(&hdma_tim3_ch4_up);
    OV7670_FIFO_OE_High();
    LCD_CS_High();
    s_abort_pending = false;
  }

  /* Reset FIFO read pointer — AL422B requires RRST low + at least one RCK
   * falling edge to reset the read address. Without this pulse the read
   * pointer never returns to 0, causing frame-to-frame drift (dynamic tearing).
   * TIM3 is stopped at this point (FrameDone or abort path), so RCK must be
   * driven as GPIO for the pulse.
   *
   * BRR writes before each GPIO_Init ensure ODR matches the expected
   * pin level, preventing a glitch during the AF↔GPIO mode transition. */
  {
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = OV7670_FIFO_RCK_Pin;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    /* Switch to GPIO output: pre-clear ODR so no glitch on mode change */
    OV7670_FIFO_RCK_GPIO_Port->BRR = OV7670_FIFO_RCK_Pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(OV7670_FIFO_RCK_GPIO_Port, &gpio);

    /* AL422B read-pointer reset sequence */
    HAL_GPIO_WritePin(OV7670_FIFO_RCK_GPIO_Port, OV7670_FIFO_RCK_Pin, GPIO_PIN_SET);
    OV7670_FIFO_RRST_Low();
    HAL_GPIO_WritePin(OV7670_FIFO_RCK_GPIO_Port, OV7670_FIFO_RCK_Pin, GPIO_PIN_RESET);  /* falling edge while RRST low -> reset */
    OV7670_FIFO_RRST_High();

    /* Restore RCK to AF: pre-clear ODR to match AF idle low */
    OV7670_FIFO_RCK_GPIO_Port->BRR = OV7670_FIFO_RCK_Pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(OV7670_FIFO_RCK_GPIO_Port, &gpio);
  }

  /* Enable FIFO output */
  OV7670_FIFO_OE_Low();

  /* Set LCD address window (leaves CS low, DC high for pixel stream) */
  LCD_SetAddrWindow(0u, 0u, PIPELINE_WIDTH - 1u, PIPELINE_HEIGHT - 1u);

  /* Reset byte counter */
  s_bytes_sent = 0u;
  s_spi_dma_busy = false;

  /*
   * Start Camera DMA: GPIOA->IDR -> s_pipeline_buffer[640], Circular
   *
   * Set callback function pointers before starting DMA.
   * TIM3 CC4 DMA request triggers one transfer per RCK cycle.
   */
  hdma_tim3_ch4_up.XferHalfCpltCallback = DmaHalfCpltCb;
  hdma_tim3_ch4_up.XferCpltCallback = DmaCpltCb;
  HAL_DMA_Start_IT(&hdma_tim3_ch4_up,
                    (uint32_t)OV7670_DATA_ADDR,
                    (uint32_t)s_pipeline_buffer,
                    PIPELINE_BUFFER_SIZE);

  /* Start TIM3 CH4 PWM (RCK 1.44MHz) */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

  s_state = PIPELINE_STATE_FRAME_CAPTURING;
}

/**
  * @brief   Complete frame capture and return to IDLE
  */
static void FrameDone(void)
{
  /* Wait for last SPI DMA to finish */
  while (s_spi_dma_busy)
  {
  }

  /* Stop TIM3 CH4 PWM */
  HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_4);

  /* Stop Camera DMA */
  HAL_DMA_Abort(&hdma_tim3_ch4_up);

  /* Disable FIFO output, raise CS, stop FIFO write */
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
  s_spi_dma_busy = false;
  s_frame_count = 0u;
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
  /* Reset FIFO write pointer: WRST must stay low for >= 1 WCLK cycle
   * (WCLK = PCLK = 2MHz, T_WCLK = 500ns). 1us DWT delay guarantees capture.
   * DWT_DelayUs is CYCCNT-based, ISR-safe (no SysTick / scheduler dependency). */
  OV7670_FIFO_WRST_Low();
  DWT_DelayUs(1u);
  OV7670_FIFO_WRST_High();
  OV7670_FIFO_WR_High();

  /* Flag if the previous frame's read was left running (state != IDLE), so
   * ReadStart cleans up its DMA/PWM before starting this frame. */
  s_abort_pending = (s_state != PIPELINE_STATE_IDLE);

  /* Unconditionally start a new frame read: no IDLE-state gating, so no
   * frames are dropped.  In normal timing the read finishes inside the frame
   * period (see PIPELINE_VSYNC_DELAY_US) and state is already IDLE here. */
  s_state = PIPELINE_STATE_FRAME_START;

  /* Start non-blocking delay */
  DWT_DelayStart(&s_vsync_delay, PIPELINE_VSYNC_DELAY_US);
}

static void OnDmaHalfCplt(void)
{
  if (s_state != PIPELINE_STATE_FRAME_CAPTURING)
  {
    return;
  }

  /* Buffer A [0..319] ready, send via SPI DMA */
  s_spi_dma_busy = true;
  HAL_SPI_Transmit_DMA(&hspi2, s_pipeline_buffer, PIPELINE_HALF_SIZE);
  s_bytes_sent += PIPELINE_HALF_SIZE;
}

static void OnDmaCplt(void)
{
  if (s_state != PIPELINE_STATE_FRAME_CAPTURING)
  {
    return;
  }

  /* Buffer B [320..639] ready, send via SPI DMA */
  s_spi_dma_busy = true;
  HAL_SPI_Transmit_DMA(&hspi2, &s_pipeline_buffer[PIPELINE_HALF_SIZE], PIPELINE_HALF_SIZE);
  s_bytes_sent += PIPELINE_HALF_SIZE;

  if (s_bytes_sent >= PIPELINE_FRAME_SIZE)
  {
    s_state = PIPELINE_STATE_FRAME_DONE;
  }
}

/* ---- DMA callback wrappers (function pointer signature) ---- */

static void DmaHalfCpltCb(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  OnDmaHalfCplt();
}

static void DmaCpltCb(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  OnDmaCplt();
}

/* ---- HAL weak function overrides ---- */

/** @brief  SPI DMA transmit complete callback */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
  (void)hspi;
  s_spi_dma_busy = false;
}

/** @brief  GPIO EXTI callback (VSYNC frame sync) */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin != OV7670_VSYNC_Pin)
  {
    return;
  }
  OnVsync();
}
