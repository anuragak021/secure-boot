#include "board.h"

#include "stm32h7xx.h"

#define HSI_HZ 64000000u
#define SYSTICK_RELOAD (HSI_HZ / 1000u - 1u)

#define LED_GREEN_PORT GPIOB
#define LED_GREEN_PIN 0u
#define LED_YELLOW_PORT GPIOE
#define LED_YELLOW_PIN 1u
#define LED_RED_PORT GPIOB
#define LED_RED_PIN 14u
#define BUTTON_PORT GPIOC
#define BUTTON_PIN 13u /* B1: active high on the Nucleo-144 */
#define VCP_PORT GPIOD
#define VCP_TX_PIN 8u
#define VCP_RX_PIN 9u
#define VCP_AF 7u /* USART3 */

#define MODER_INPUT 0u
#define MODER_OUTPUT 1u
#define MODER_AF 2u
#define PIN_MODE_BITS 2u
#define AFR_BITS 4u
#define PUPDR_PULL_DOWN 2u

/* Written by the SysTick ISR, read by the main loop: volatile is required. */
static volatile uint32_t g_ms;

static void gpio_set_mode(GPIO_TypeDef *port, uint32_t pin, uint32_t mode)
{
    const uint32_t shift = pin * PIN_MODE_BITS;
    port->MODER = (port->MODER & ~(3u << shift)) | (mode << shift);
}

static GPIO_TypeDef *led_port(board_led_t led)
{
    switch (led) {
    case BOARD_LED_GREEN:
        return LED_GREEN_PORT;
    case BOARD_LED_YELLOW:
        return LED_YELLOW_PORT;
    default:
        return LED_RED_PORT;
    }
}

static uint32_t led_pin(board_led_t led)
{
    switch (led) {
    case BOARD_LED_GREEN:
        return LED_GREEN_PIN;
    case BOARD_LED_YELLOW:
        return LED_YELLOW_PIN;
    default:
        return LED_RED_PIN;
    }
}

void SysTick_Handler(void)
{
    g_ms++;
}

void board_init(void)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIOCEN | RCC_AHB4ENR_GPIODEN |
                    RCC_AHB4ENR_GPIOEEN | RCC_AHB4ENR_CRCEN;
    RCC->APB1LENR |= RCC_APB1LENR_USART3EN;
    (void)RCC->APB1LENR; /* read back: make sure the clock is running before first access */

    gpio_set_mode(LED_GREEN_PORT, LED_GREEN_PIN, MODER_OUTPUT);
    gpio_set_mode(LED_YELLOW_PORT, LED_YELLOW_PIN, MODER_OUTPUT);
    gpio_set_mode(LED_RED_PORT, LED_RED_PIN, MODER_OUTPUT);
    /* The H7 resets GPIO pins to analog mode (MODER=3), where IDR always reads 0: B1 must be
     * switched to input explicitly. The Nucleo has an external pull-down on PC13. */
    gpio_set_mode(BUTTON_PORT, BUTTON_PIN, MODER_INPUT);
    BUTTON_PORT->PUPDR = (BUTTON_PORT->PUPDR & ~(3u << (BUTTON_PIN * PIN_MODE_BITS))) |
                         (PUPDR_PULL_DOWN << (BUTTON_PIN * PIN_MODE_BITS));

    gpio_set_mode(VCP_PORT, VCP_TX_PIN, MODER_AF);
    gpio_set_mode(VCP_PORT, VCP_RX_PIN, MODER_AF);
    VCP_PORT->AFR[1] = (VCP_PORT->AFR[1] & ~(0xFFu)) | (VCP_AF << ((VCP_TX_PIN - 8u) * AFR_BITS)) |
                       (VCP_AF << ((VCP_RX_PIN - 8u) * AFR_BITS));

    g_ms = 0u;
    SysTick->LOAD = SYSTICK_RELOAD;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

void board_deinit(void)
{
    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;

    /* Pulse the reset lines of everything we touched so the app starts from reset defaults. */
    RCC->APB1LRSTR |= RCC_APB1LRSTR_USART3RST;
    RCC->APB1LRSTR &= ~RCC_APB1LRSTR_USART3RST;
    RCC->AHB4RSTR |= RCC_AHB4RSTR_CRCRST | RCC_AHB4RSTR_GPIOBRST | RCC_AHB4RSTR_GPIOCRST |
                     RCC_AHB4RSTR_GPIODRST | RCC_AHB4RSTR_GPIOERST;
    RCC->AHB4RSTR &= ~(RCC_AHB4RSTR_CRCRST | RCC_AHB4RSTR_GPIOBRST | RCC_AHB4RSTR_GPIOCRST |
                       RCC_AHB4RSTR_GPIODRST | RCC_AHB4RSTR_GPIOERST);
    RCC->APB1LENR &= ~RCC_APB1LENR_USART3EN;
    RCC->AHB4ENR &= ~(RCC_AHB4ENR_GPIOBEN | RCC_AHB4ENR_GPIOCEN | RCC_AHB4ENR_GPIODEN |
                      RCC_AHB4ENR_GPIOEEN | RCC_AHB4ENR_CRCEN);
}

void board_led_set(board_led_t led, bool on)
{
    const uint32_t pin = led_pin(led);
    /* BSRR: low half sets, high half resets; atomic, no read-modify-write. */
    led_port(led)->BSRR = on ? (1u << pin) : (1u << (pin + 16u));
}

void board_led_toggle(board_led_t led)
{
    led_port(led)->ODR ^= (1u << led_pin(led));
}

bool board_button_pressed(void)
{
    return (BUTTON_PORT->IDR & (1u << BUTTON_PIN)) != 0u;
}

uint32_t board_millis(void)
{
    return g_ms;
}

void board_delay_ms(uint32_t ms)
{
    const uint32_t start = g_ms;
    while ((g_ms - start) < ms) {
    }
}
