#include "transport_uart.h"

#include "board.h"
#include "stm32h7xx.h"

#define BAUD 115200u
#define HSI_HZ 64000000u
#define RX_RING_BYTES 1024u /* power of two: indices wrap with a mask */
#define RX_RING_MASK (RX_RING_BYTES - 1u)
#define TX_WAIT_LIMIT 0x00FFFFFFu

_Static_assert((RX_RING_BYTES & (RX_RING_BYTES - 1u)) == 0u, "ring size must be a power of two");

/* Shared between the USART3 ISR (producer) and the main loop (consumer). Single core, one
 * writer per index: head is only written by the ISR, tail only by the main loop, so volatile
 * plus a data barrier is enough. */
static uint8_t g_ring[RX_RING_BYTES];
static volatile uint32_t g_head;
static volatile uint32_t g_tail;
static volatile uint32_t g_drops;

void USART3_IRQHandler(void)
{
    const uint32_t isr = USART3->ISR;
    if ((isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) != 0u) {
        /* Overrun happens while the CPU is stalled by a flash erase. Without clearing ORE
         * the receiver would stop for good. */
        USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
        g_drops++;
    }
    while ((USART3->ISR & USART_ISR_RXNE_RXFNE) != 0u) {
        const uint8_t b = (uint8_t)USART3->RDR; /* reading RDR clears RXNE */
        const uint32_t head = g_head;
        if (((head + 1u) & RX_RING_MASK) == g_tail) {
            g_drops++; /* ring full: drop the byte, the protocol CRC will catch the damage */
        } else {
            g_ring[head] = b;
            __DMB();
            g_head = (head + 1u) & RX_RING_MASK;
        }
    }
}

static bl_status_t uart_init(void)
{
    g_head = 0u;
    g_tail = 0u;
    g_drops = 0u;
    USART3->CR1 = 0u;
    USART3->BRR = (HSI_HZ + BAUD / 2u) / BAUD;
    USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    USART3->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_UE;
    NVIC_ClearPendingIRQ(USART3_IRQn);
    NVIC_EnableIRQ(USART3_IRQn);
    return BL_OK;
}

static bl_status_t uart_send(const uint8_t *buf, size_t len)
{
    if ((buf == NULL) && (len != 0u)) {
        return BL_ERR_INVALID_ARG;
    }
    for (size_t i = 0u; i < len; i++) {
        uint32_t guard = TX_WAIT_LIMIT;
        while (((USART3->ISR & USART_ISR_TXE_TXFNF) == 0u) && (guard > 0u)) {
            guard--;
        }
        if (guard == 0u) {
            return BL_ERR_TRANSPORT;
        }
        USART3->TDR = buf[i];
    }
    uint32_t guard = TX_WAIT_LIMIT;
    while (((USART3->ISR & USART_ISR_TC) == 0u) && (guard > 0u)) {
        guard--;
    }
    return (guard == 0u) ? BL_ERR_TRANSPORT : BL_OK;
}

static bl_status_t uart_recv(uint8_t *buf, size_t maxlen, size_t *out_len, uint32_t timeout_ms)
{
    if ((buf == NULL) || (out_len == NULL)) {
        return BL_ERR_INVALID_ARG;
    }
    *out_len = 0u;
    const uint32_t start = board_millis();
    while (g_head == g_tail) {
        if ((board_millis() - start) >= timeout_ms) {
            return BL_ERR_TIMEOUT;
        }
    }
    __DMB();
    size_t n = 0u;
    uint32_t tail = g_tail;
    while ((n < maxlen) && (tail != g_head)) {
        buf[n] = g_ring[tail];
        n++;
        tail = (tail + 1u) & RX_RING_MASK;
    }
    g_tail = tail;
    *out_len = n;
    return BL_OK;
}

const transport_t g_transport_uart = {uart_init, uart_send, uart_recv};

uint32_t transport_uart_rx_drops(void)
{
    return g_drops;
}
