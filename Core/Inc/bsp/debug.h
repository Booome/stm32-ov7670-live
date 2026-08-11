/**
  * @file    debug.h
  * @brief   Debug output via USART1 (printf redirection)
  *
  *          debug_printf macro: active when DEBUG or FORCE_DEBUG_OUTPUT
  *          is defined.  Uses newlib printf -> _write() -> __io_putchar()
  *          -> HAL_UART_Transmit.
  *
  *          To enable in Release builds:
  *            cmake --preset Release -DFORCE_DEBUG_OUTPUT=ON
  *
  *          WARNING: Do NOT use debug_printf in interrupt context (blocking UART).
  *          newlib-nano does not support %f float formatting.
  */
#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

#if defined(DEBUG) || defined(FORCE_DEBUG_OUTPUT)
  #define debug_printf(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
  #define debug_printf(fmt, ...) ((void)0)
#endif

#endif /* DEBUG_H */
