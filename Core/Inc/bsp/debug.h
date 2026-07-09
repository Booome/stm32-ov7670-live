/**
  * @file    debug.h
  * @brief   Debug output via USART1 (printf redirection)
  *
  *          debug_printf macro: active in Debug build, no-op in Release.
  *          Uses newlib printf -> _write() -> __io_putchar() -> HAL_UART_Transmit.
  *
  *          WARNING: Do NOT use debug_printf in interrupt context (blocking UART).
  *          newlib-nano does not support %f float formatting.
  */
#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

#ifdef DEBUG
  #define debug_printf(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
  #define debug_printf(fmt, ...) ((void)0)
#endif

#endif /* DEBUG_H */
