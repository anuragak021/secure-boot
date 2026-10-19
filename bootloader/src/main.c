/* Bootloader entry. Step 5 version: validate the app slot and jump, otherwise stay and report. */
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "boot.h"
#include "boot_check.h"
#include "crc_hw_port.h"
#include "flash_layout.h"
#include "flash_ops.h"
#include "stm32h7xx.h"

#define BAUD 115200u
#define HSI_HZ 64000000u
#define STAY_BLINK_MS 250u

static void uart_init(void)
{
    USART3->BRR = (HSI_HZ + BAUD / 2u) / BAUD;
    USART3->CR1 = USART_CR1_TE | USART_CR1_UE;
}

static void uart_puts(const char *s)
{
    while (*s != '\0') {
        while ((USART3->ISR & USART_ISR_TXE_TXFNF) == 0u) {
        }
        USART3->TDR = (uint32_t)(uint8_t)*s++;
    }
    while ((USART3->ISR & USART_ISR_TC) == 0u) {
    }
}

int main(void)
{
    board_init();
    uart_init();
    uart_puts("\r\nBL: start\r\n");

    const bl_status_t crc_ok = crc_hw_init();
    if (crc_ok != BL_OK) {
        uart_puts("BL: hardware CRC self test FAILED, refusing to boot\r\n");
    }

    const flash_ops_t ops = {NULL, NULL, crc_hw_flash_crc32, NULL};
    image_header_t hdr;
    const bl_status_t app =
        (crc_ok == BL_OK) ? boot_check_slot((const uint8_t *)BL_APP_SLOT_BASE_ADDR, &ops, &hdr)
                          : BL_ERR_VERIFY_FAIL;

    if ((app == BL_OK) && !board_button_pressed()) {
        uart_puts("BL: app valid, jumping\r\n");
        boot_jump_to_app();
    }

    uart_puts((app == BL_OK) ? "BL: button held, staying\r\n" : "BL: no valid app, staying\r\n");
    for (;;) {
        board_led_toggle(BOARD_LED_RED);
        board_delay_ms(STAY_BLINK_MS);
    }
}
