/**
  * @file    debug.c
  * @brief   Debug output implementation - printf redirection to USART1
  */
#include "stm32f1xx_hal.h"

/* USART1 handle (defined in main.c) */
extern UART_HandleTypeDef huart1;

/**
  * @brief  Write one character to USART1 (non-weak override)
  * @param  ch  Character to send (cast to uint8_t)
  * @retval ch  Character sent
  *
  *         Called by newlib _write() -> __io_putchar() chain.
  *         Blocking transmit with timeout.
  */
int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  HAL_UART_Transmit(&huart1, &c, 1u, HAL_MAX_DELAY);
  return ch;
}
