/**
 * @file transport_uart.h
 * @brief USART3 (ST-Link virtual COM port) transport: interrupt driven RX into a ring buffer.
 */
#ifndef TRANSPORT_UART_H
#define TRANSPORT_UART_H

#include <stdint.h>

#include "transport.h"

/** @brief The UART implementation of transport_t. */
extern const transport_t g_transport_uart;

/** @brief Number of received bytes dropped because the ring was full or the UART overran. */
uint32_t transport_uart_rx_drops(void);

#endif /* TRANSPORT_UART_H */
