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

/* VSYNC delay: 1.3ms (back porch 1.11ms + 190us safety margin) */
#define PIPELINE_VSYNC_DELAY_US  1300u
_Static_assert(PIPELINE_VSYNC_DELAY_US > 1110u,
              "VSYNC delay must exceed OV7670 back porch (1.11ms @ 12MHz)");

/* Module state */
static volatile Pipeline_StateTypeDef s_state = PIPELINE_STATE_DISABLED;
static volatile uint32_t s_bytes_sent;
static volatile bool s_spi_dma_busy;
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
  /* Reset FIFO read pointer */
  OV7670_FIFO_RRST_Low();
  OV7670_FIFO_RRST_High();

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

  s_state = PIPELINE_STATE_IDLE;
}

void Pipeline_Init(void)
{
  s_state = PIPELINE_STATE_IDLE;
  s_bytes_sent = 0u;
  s_spi_dma_busy = false;
}

Pipeline_StateTypeDef Pipeline_GetState(void)
{
  return s_state;
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
  if (s_state != PIPELINE_STATE_IDLE)
  {
    return;  /* Drop frame if busy */
  }

  s_state = PIPELINE_STATE_FRAME_START;

  /* Reset FIFO write pointer */
  OV7670_FIFO_WRST_Low();
  OV7670_FIFO_WRST_High();

  /* Enable FIFO write (NAND gate with HREF) */
  OV7670_FIFO_WR_High();

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
