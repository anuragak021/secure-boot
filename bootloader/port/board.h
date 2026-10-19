/**
 * @file board.h
 * @brief Nucleo-H753ZI board support: clocks stay on HSI 64 MHz, LEDs, user button, millisecond
 * tick.
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { BOARD_LED_GREEN = 0, BOARD_LED_YELLOW = 1, BOARD_LED_RED = 2 } board_led_t;

/** @brief Enable GPIO/USART clocks, configure LEDs, button B1 (PC13), VCP pins; start 1 ms tick. */
void board_init(void);

/**
 * @brief Return every peripheral the bootloader used to its reset state and stop SysTick.
 * @note Called right before jumping to the application.
 */
void board_deinit(void);

/** @brief Switch an LED on or off. */
void board_led_set(board_led_t led, bool on);

/** @brief Toggle an LED. */
void board_led_toggle(board_led_t led);

/** @brief True while user button B1 is held down. */
bool board_button_pressed(void);

/** @brief Milliseconds since board_init() (wraps after ~49 days; compare with subtraction). */
uint32_t board_millis(void);

/** @brief Busy wait for ms milliseconds. */
void board_delay_ms(uint32_t ms);

#endif /* BOARD_H */
