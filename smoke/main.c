/* Hardware smoke test: blinks LD1..LD3 in turn and prints a line on the ST-Link VCP.
 * Proves clocks, pins (LED PB0/PE1/PB14, USART3 PD8/PD9) and the flashing path. */
#include "stm32h7xx.h"
#include <stdint.h>

#define HSI_HZ 64000000u
#define BAUD 115200u
#define SYSTICK_PER_MS (HSI_HZ / 1000u)
#define USART_AF 7u

static void delay_ms(uint32_t ms)
{
    SysTick->LOAD = SYSTICK_PER_MS - 1u;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
    while (ms > 0u) {
        while ((SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) == 0u) {
        }
        ms--;
    }
}

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        while ((USART3->ISR & USART_ISR_TXE_TXFNF) == 0u) {
        }
        USART3->TDR = (uint32_t)(uint8_t)*s++;
    }
}

int main(void)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIODEN | RCC_AHB4ENR_GPIOEEN;
    RCC->APB1LENR |= RCC_APB1LENR_USART3EN;
    (void)RCC->APB1LENR;

    /* LEDs: PB0, PB14, PE1 as outputs. */
    GPIOB->MODER = (GPIOB->MODER & ~((3u << 0) | (3u << 28))) | (1u << 0) | (1u << 28);
    GPIOE->MODER = (GPIOE->MODER & ~(3u << 2)) | (1u << 2);

    /* PD8/PD9: alternate function 7 (USART3). */
    GPIOD->MODER = (GPIOD->MODER & ~((3u << 16) | (3u << 18))) | (2u << 16) | (2u << 18);
    GPIOD->AFR[1] = (GPIOD->AFR[1] & ~(0xFFu)) | (USART_AF << 0) | (USART_AF << 4);

    USART3->BRR = (HSI_HZ + BAUD / 2u) / BAUD;
    USART3->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    for (;;) {
        GPIOB->ODR ^= (1u << 0);
        uart_puts("hello from smoke test\r\n");
        delay_ms(250);
        GPIOE->ODR ^= (1u << 1);
        delay_ms(250);
        GPIOB->ODR ^= (1u << 14);
        delay_ms(250);
    }
}
