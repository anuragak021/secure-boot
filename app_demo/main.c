/* Demo application for the bootloader. Variants are selected at build time:
 *   APP_NAME, APP_LED_PORT/APP_LED_PIN, APP_HALF_PERIOD_MS.
 * Fast: LD1 (green, PB0) 100 ms. Slow: LD3 (red, PB14) 1000 ms. */
#include "stm32h7xx.h"
#include <stdint.h>

#define HSI_HZ 64000000u
#define BAUD 115200u
#define SYSTICK_PER_MS (HSI_HZ / 1000u)
#define USART_AF 7u
#define BANNER_PERIOD_MS 2000u

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
    /* The bootloader jumps here with interrupts disabled; this app re-enables them. */
    __enable_irq();
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIODEN;
    RCC->APB1LENR |= RCC_APB1LENR_USART3EN;
    (void)RCC->APB1LENR;

    APP_LED_PORT->MODER =
        (APP_LED_PORT->MODER & ~(3u << (2u * APP_LED_PIN))) | (1u << (2u * APP_LED_PIN));

    GPIOD->MODER = (GPIOD->MODER & ~((3u << 16) | (3u << 18))) | (2u << 16) | (2u << 18);
    GPIOD->AFR[1] = (GPIOD->AFR[1] & ~(0xFFu)) | (USART_AF << 0) | (USART_AF << 4);
    USART3->BRR = (HSI_HZ + BAUD / 2u) / BAUD;
    USART3->CR1 = USART_CR1_TE | USART_CR1_UE;

    uint32_t since_banner_ms = BANNER_PERIOD_MS;
    for (;;) {
        APP_LED_PORT->ODR ^= (1u << APP_LED_PIN);
        if (since_banner_ms >= BANNER_PERIOD_MS) {
            uart_puts("app " APP_NAME " running\r\n");
            since_banner_ms = 0u;
        }
        delay_ms(APP_HALF_PERIOD_MS);
        since_banner_ms += APP_HALF_PERIOD_MS;
    }
}
