/**
  * @file    camera.c
  * @brief   Frame capture state machine and DMA pipeline implementation
  */
#include "camera.h"
#include "st7735.h"
#include "ov7670.h"
#include "dwt_delay.h"
#include "main.h"
#include "stm32f1xx_hal.h"

/* External handles (defined in main.c) */
extern TIM_HandleTypeDef htim3;
extern SPI_HandleTypeDef hspi2;
extern DMA_HandleTypeDef hdma_tim3_ch4_up;
extern DMA_HandleTypeDef hdma_spi2_tx;

/* GPIOA IDR address for Camera DMA source (defined in main.h) */
/* OV7670_DATA_ADDR = (&(OV7670_DATA_PORT->IDR)) */

/* VSYNC delay: 2ms @ 72MHz = 144000 cycles */
#define CAMERA_VSYNC_DELAY_US  2000u

/* Module state */
static volatile Camera_StateTypeDef s_state = CAMERA_STATE_IDLE;
static volatile uint32_t s_bytes_sent;
static volatile bool s_spi_dma_busy;
static DWT_DelayHandle s_vsync_delay;

/* Frame buffer: 640 bytes, 2 x 320B ping-pong */
static uint8_t CameraBuffer[CAMERA_BUFFER_SIZE];

/* Forward declarations for DMA callback wrappers */
static void camera_dma_half_cplt_cb(DMA_HandleTypeDef *hdma);
static void camera_dma_cplt_cb(DMA_HandleTypeDef *hdma);

/**
  * @brief   Start FIFO read pipeline
  *
  *          Called after VSYNC delay expires. Sets up FIFO read pointer,
  *          LCD address window, then starts TIM3 PWM + Camera DMA.
  */
static void read_start(void)
{
  /* Reset FIFO read pointer */
  OV7670_FIFO_RRST_Low();
  OV7670_FIFO_RRST_High();

  /* Enable FIFO output */
  OV7670_FIFO_OE_Low();

  /* Set LCD address window (leaves CS low, DC high for pixel stream) */
  LCD_SetAddrWindow(0u, 0u, CAMERA_WIDTH - 1u, CAMERA_HEIGHT - 1u);

  /* Reset byte counter */
  s_bytes_sent = 0u;
  s_spi_dma_busy = false;

  /*
   * Start Camera DMA: GPIOA->IDR -> CameraBuffer[640], Circular
   *
   * Set callback function pointers before starting DMA.
   * TIM3 CC4 DMA request triggers one transfer per RCK cycle.
   */
  hdma_tim3_ch4_up.XferHalfCpltCallback = camera_dma_half_cplt_cb;
  hdma_tim3_ch4_up.XferCpltCallback = camera_dma_cplt_cb;
  HAL_DMA_Start_IT(&hdma_tim3_ch4_up,
                    (uint32_t)OV7670_DATA_ADDR,
                    (uint32_t)CameraBuffer,
                    CAMERA_BUFFER_SIZE);

  /* Start TIM3 CH4 PWM (RCK 1.44MHz) */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);

  s_state = CAMERA_STATE_FRAME_CAPTURING;
}

/**
  * @brief   Complete frame capture and return to IDLE
  */
static void frame_done(void)
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

  s_state = CAMERA_STATE_IDLE;
}

void Camera_Init(void)
{
  s_state = CAMERA_STATE_IDLE;
  s_bytes_sent = 0u;
  s_spi_dma_busy = false;
}

Camera_StateTypeDef Camera_GetState(void)
{
  return s_state;
}

void Camera_Poll(void)
{
  if (s_state == CAMERA_STATE_FRAME_START)
  {
    if (DWT_DelayExpired(&s_vsync_delay))
    {
      read_start();
    }
  }
  else if (s_state == CAMERA_STATE_FRAME_DONE)
  {
    frame_done();
  }
}

void Camera_OnVsync(void)
{
  if (s_state != CAMERA_STATE_IDLE)
  {
    return;  /* Drop frame if busy */
  }

  s_state = CAMERA_STATE_FRAME_START;

  /* Reset FIFO write pointer */
  OV7670_FIFO_WRST_Low();
  OV7670_FIFO_WRST_High();

  /* Enable FIFO write (NAND gate with HREF) */
  OV7670_FIFO_WR_High();

  /* Start non-blocking 2ms delay */
  DWT_DelayStart(&s_vsync_delay, CAMERA_VSYNC_DELAY_US);
}

void Camera_OnDmaHalfCplt(void)
{
  if (s_state != CAMERA_STATE_FRAME_CAPTURING)
  {
    return;
  }

  /* Buffer A [0..319] ready, send via SPI DMA */
  s_spi_dma_busy = true;
  HAL_SPI_Transmit_DMA(&hspi2, CameraBuffer, CAMERA_HALF_SIZE);
  s_bytes_sent += CAMERA_HALF_SIZE;
}

void Camera_OnDmaCplt(void)
{
  if (s_state != CAMERA_STATE_FRAME_CAPTURING)
  {
    return;
  }

  /* Buffer B [320..639] ready, send via SPI DMA */
  s_spi_dma_busy = true;
  HAL_SPI_Transmit_DMA(&hspi2, &CameraBuffer[CAMERA_HALF_SIZE], CAMERA_HALF_SIZE);
  s_bytes_sent += CAMERA_HALF_SIZE;

  if (s_bytes_sent >= CAMERA_FRAME_SIZE)
  {
    s_state = CAMERA_STATE_FRAME_DONE;
  }
}

void Camera_OnSpiDmaCplt(void)
{
  s_spi_dma_busy = false;
}

/* ---- DMA callback wrappers (function pointer signature) ---- */

static void camera_dma_half_cplt_cb(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  Camera_OnDmaHalfCplt();
}

static void camera_dma_cplt_cb(DMA_HandleTypeDef *hdma)
{
  (void)hdma;
  Camera_OnDmaCplt();
}

/* ---- HAL weak function overrides ---- */

/** @brief  SPI DMA transmit complete callback */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
  (void)hspi;
  Camera_OnSpiDmaCplt();
}

/** @brief  GPIO EXTI callback (VSYNC frame sync) */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin != OV7670_VSYNC_Pin)
  {
    return;
  }
  Camera_OnVsync();
}
