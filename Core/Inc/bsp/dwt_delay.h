/**
  * @file    dwt_delay.h
  * @brief   DWT CYCCNT based delay module
  *
  *          Provides blocking and non-blocking delays using the
  *          Cortex-M3 DWT cycle counter (CYCCNT). Accuracy is one
  *          CPU cycle (13.9 ns @ 72 MHz).
  *
  *          CYCCNT wrap-around (every ~59.65 s @ 72 MHz) is handled
  *          correctly via unsigned arithmetic.
  */
#ifndef DWT_DELAY_H
#define DWT_DELAY_H

#include <stdint.h>
#include <stdbool.h>

/** @brief Non-blocking delay handle (caller-owned instance) */
typedef struct
{
  volatile uint32_t start_cycles;  /**< CYCCNT snapshot at start       */
  volatile uint32_t target_ticks;  /**< cycles to wait                 */
  volatile uint8_t  active;        /**< 1 = pending, 0 = idle/expired  */
} DWT_DelayHandle;

/** @brief  Initialize DWT cycle counter (call once before any DWT_Delay*) */
void     DWT_Init(void);

/** @brief  Get current cycle count */
uint32_t DWT_GetCycles(void);

/** @brief  Blocking delay in microseconds */
void     DWT_DelayUs(uint32_t us);

/** @brief  Blocking delay in milliseconds */
void     DWT_DelayMs(uint32_t ms);

/** @brief  Start non-blocking delay on given handle
  * @param  h   Handle instance (caller-owned)
  * @param  us  Delay duration in microseconds
  */
void     DWT_DelayStart(DWT_DelayHandle *h, uint32_t us);

/** @brief  Check if non-blocking delay has expired
  * @param  h   Handle instance
  * @retval true  Delay expired (active flag auto-cleared)
  * @retval false Not expired or not started
  */
bool     DWT_DelayExpired(DWT_DelayHandle *h);

/** @brief  Cancel a pending non-blocking delay */
void     DWT_DelayCancel(DWT_DelayHandle *h);

#endif /* DWT_DELAY_H */
