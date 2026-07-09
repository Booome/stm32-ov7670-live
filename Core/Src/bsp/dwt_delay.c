/**
  * @file    dwt_delay.c
  * @brief   DWT CYCCNT based delay module implementation
  */
#include "dwt_delay.h"
#include "stm32f1xx_hal.h"

/* Cached clock parameters - refreshed when SystemCoreClock changes */
static uint32_t s_cycles_per_us;
static uint32_t s_max_us;
static uint32_t s_max_us_ticks;   /* s_max_us * s_cycles_per_us  */
static uint32_t s_ticks_per_ms;
static uint32_t s_max_ms;
static uint32_t s_max_ms_ticks;   /* s_max_ms * s_ticks_per_ms  */
static uint32_t s_cached_clock;

/**
  * @brief   Refresh cached clock parameters from SystemCoreClock
  *
  *          Called from DWT_Init() and lazily when a delay function
  *          detects that SystemCoreClock has changed.
  */
static void dwt_refresh_cache(void)
{
  s_cycles_per_us = SystemCoreClock / 1000000u;
  s_max_us        = UINT32_MAX / s_cycles_per_us;
  s_max_us_ticks  = s_max_us * s_cycles_per_us;
  s_ticks_per_ms  = s_cycles_per_us * 1000u;
  s_max_ms        = UINT32_MAX / s_ticks_per_ms;
  s_max_ms_ticks  = s_max_ms * s_ticks_per_ms;
  s_cached_clock  = SystemCoreClock;
}

/**
  * @brief   Check and refresh cache if SystemCoreClock changed
  *
  *          Overhead: 1 volatile load + 1 compare + 1 branch (~2 cycles
  *          when clock unchanged, branch predicted not-taken).
  */
static inline void dwt_check_refresh(void)
{
  if (SystemCoreClock != s_cached_clock)
  {
    dwt_refresh_cache();
  }
}

/**
  * @brief   Convert microseconds to cycle ticks with overflow protection
  * @param   us  Delay in microseconds
  * @retval  Target cycle count
  *
  *          Used only by DWT_DelayStart (non-blocking, cannot chunk).
  *          If us exceeds the safe maximum (UINT32_MAX / cycles_per_us),
  *          it is saturated to that maximum. The resulting delay will be
  *          at least as long as requested, never shorter.
  */
static inline uint32_t dwt_us_to_ticks(uint32_t us)
{
  assert_param(us <= s_max_us);
  if (us > s_max_us)
  {
    us = s_max_us;
  }
  return us * s_cycles_per_us;
}

/**
  * @brief   Block until target_ticks cycles have elapsed
  * @param   cycles  Number of CPU cycles to wait
  *
  *          CYCCNT wrap-around safe: unsigned subtraction yields the
  *          correct elapsed cycle count even when CYCCNT wraps from
  *          0xFFFFFFFF to 0x00000000 mid-delay.
  */
void DWT_DelayCycles(uint32_t cycles)
{
  uint32_t start = DWT->CYCCNT;
  while ((DWT->CYCCNT - start) < cycles)
  {
  }
}

void DWT_Init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  dwt_refresh_cache();
}

uint32_t DWT_GetCycles(void)
{
  return DWT->CYCCNT;
}

void DWT_DelayUs(uint32_t us)
{
  dwt_check_refresh();

  if (us <= s_max_us)
  {
    /* Fast path: single wait, no loop */
    DWT_DelayCycles(us * s_cycles_per_us);
    return;
  }

  /* Slow path: chunk to avoid multiplication overflow */
  while (us > s_max_us)
  {
    DWT_DelayCycles(s_max_us_ticks);
    us -= s_max_us;
  }
  if (us > 0u)
  {
    DWT_DelayCycles(us * s_cycles_per_us);
  }
}

void DWT_DelayMs(uint32_t ms)
{
  dwt_check_refresh();

  if (ms <= s_max_ms)
  {
    /* Fast path: single wait, no loop */
    DWT_DelayCycles(ms * s_ticks_per_ms);
    return;
  }

  /* Slow path: chunk to avoid multiplication overflow */
  while (ms > s_max_ms)
  {
    DWT_DelayCycles(s_max_ms_ticks);
    ms -= s_max_ms;
  }
  if (ms > 0u)
  {
    DWT_DelayCycles(ms * s_ticks_per_ms);
  }
}

void DWT_DelayStart(DWT_DelayHandle *h, uint32_t us)
{
  dwt_check_refresh();
  h->start_cycles = DWT->CYCCNT;
  h->target_ticks = dwt_us_to_ticks(us);
  h->active = 1u;
}

bool DWT_DelayExpired(DWT_DelayHandle *h)
{
  if (h->active == 0u)
  {
    return false;
  }
  if ((DWT->CYCCNT - h->start_cycles) >= h->target_ticks)
  {
    h->active = 0u;
    return true;
  }
  return false;
}

void DWT_DelayCancel(DWT_DelayHandle *h)
{
  h->active = 0u;
}
