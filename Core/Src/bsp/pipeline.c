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
static void OnDmaHalfCplt(void);
static void OnDmaCplt(void);

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

/* ---- Camera DMA LL helper (DMA1_Channel3, circular, interrupt-driven) ----
 *
 * Camera DMA reads OV7670_DATA_ADDR -> s_pipeline_buffer (P2M, circular).
 * HT/TC interrupts are serviced by DMA1_Channel3_IRQHandler ->
 * HAL_DMA_IRQHandler -> OnDmaHalfCplt/OnDmaCplt.  One-time setup (CPAR,
 * DIR/MINC/PSIZE/MSIZE/CIRC/PRIORITY, NVIC) is done in HAL_DMA_Init (CubeMX).
 * Per-frame restart mirrors HAL_DMA_Start_IT: disable EN, clear flags,
 * set CNDTR/CPAR/CMAR, re-enable TC|HT|TE, then EN.  hdma State/ErrorCode
 * stay in sync so HAL_DMA_Abort (FrameDone / abort path) still sees BUSY. */

/** @brief  Start camera DMA with LL registers (mirrors HAL_DMA_Start_IT) */
static inline void CamDma_Start(void)
{
  hdma_tim3_ch4_up.State = HAL_DMA_STATE_BUSY;
  hdma_tim3_ch4_up.ErrorCode = HAL_DMA_ERROR_NONE;

  DMA1_Channel3->CCR &= ~DMA_CCR_EN;
  DMA1->IFCR = DMA_IFCR_CGIF3;
  DMA1_Channel3->CNDTR = PIPELINE_BUFFER_SIZE;
  DMA1_Channel3->CPAR = (uint32_t)OV7670_DATA_ADDR;
  DMA1_Channel3->CMAR = (uint32_t)s_pipeline_buffer;
  DMA1_Channel3->CCR |= DMA_CCR_TCIE | DMA_CCR_HTIE | DMA_CCR_TEIE;
  DMA1_Channel3->CCR |= DMA_CCR_EN;
}

/* RCK = PB1: CNF1[1:0]+MODE1[1:0] occupy CRL bits [7:4].
 * LL equivalents of HAL_GPIO_Init for the AF<->GPIO toggle in ReadStart. */
#define RCK_CRL_FIELD_MASK  (0x0Fu << 4u)
#define RCK_CRL_OUTPUT_PP   (0x03u << 4u)   /* CNF=00, MODE=11 (50MHz out) */
#define RCK_CRL_AF_PP       (0x0Bu << 4u)   /* CNF=10, MODE=11 (50MHz AF)  */

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

  /* Reset FIFO read pointer: AL422B requires RRST low + RCK falling edge.
   * Toggle RCK (PB1) AF->GPIO->AF via direct CRL writes.
   * BRR pre-clears ODR so each mode transition (which makes the pin follow
   * ODR) never glitches — ODR is already low, matching the AF idle level. */
  {
    GPIO_TypeDef *port = OV7670_FIFO_RCK_GPIO_Port;
    const uint32_t pin = OV7670_FIFO_RCK_Pin;

    port->BRR = pin;
    MODIFY_REG(port->CRL, RCK_CRL_FIELD_MASK, RCK_CRL_OUTPUT_PP);

    port->BSRR = pin;                 /* RCK high */
    OV7670_FIFO_RRST_Low();
    port->BRR = pin;                  /* RCK falling edge while RRST low */
    OV7670_FIFO_RRST_High();

    port->BRR = pin;
    MODIFY_REG(port->CRL, RCK_CRL_FIELD_MASK, RCK_CRL_AF_PP);
  }

  OV7670_FIFO_OE_Low();

  LCD_SetAddrWindow(0u, 0u, PIPELINE_WIDTH - 1u, PIPELINE_HEIGHT - 1u);

  /* LCD_SetAddrWindow used blocking HAL_SPI_Transmit: restore 1-line TX */
  SpiRestore1LineTx();

  s_bytes_sent = 0u;

  CamDma_Start();

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

/** @brief  Camera DMA (DMA1_Channel3) interrupt entry.
  *         Replaces HAL_DMA_IRQHandler: reads ISR, clears the HT/TC flag,
  *         dispatches to the matching handler.  Circular mode -> no interrupt
  *         disable or state change on completion (mirrors HAL behavior). */
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

/** @brief  VSYNC EXTI (PA11) interrupt entry.
  *         Replaces HAL_GPIO_EXTI_IRQHandler + HAL_GPIO_EXTI_Callback:
  *         clears the EXTI pending flag and starts a new frame read. */
void Pipeline_VsyncExtiIrq(void)
{
  if ((EXTI->PR & OV7670_VSYNC_Pin) != 0u)
  {
    EXTI->PR = OV7670_VSYNC_Pin;
    OnVsync();
  }
}
