/* Bootloader entry: boot decision, HELLO window, UART update loop. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "boot.h"
#include "boot_check.h"
#include "crc_hw_port.h"
#include "flash_layout.h"
#include "flash_ops.h"
#include "flash_port.h"
#include "protocol.h"
#include "protocol_codec.h"
#include "session.h"
#include "transport_uart.h"
#include "updater.h"

#define HELLO_WINDOW_MS 500u /* wait this long for a HELLO before booting a valid app */
#define POLL_TIMEOUT_MS 10u
#define RX_CHUNK_BYTES 64u
#define ERROR_BLINK_MS 250u
#define BANNER_PERIOD_MS 1000u

static const transport_t *const g_tp = &g_transport_uart;

static void log_line(const char *s)
{
    size_t n = 0u;
    while (s[n] != '\0') {
        n++;
    }
    (void)g_tp->send((const uint8_t *)s, n);
}

/** Feed pending UART bytes to the frame receiver. Returns true if any frame was handled. */
static bool pump(updater_t *upd, proto_rx_t *rx, uint32_t timeout_ms)
{
    uint8_t chunk[RX_CHUNK_BYTES];
    size_t got = 0u;
    bool handled = false;
    if (g_tp->recv(chunk, sizeof(chunk), &got, timeout_ms) != BL_OK) {
        return false;
    }
    for (size_t i = 0u; i < got; i++) {
        bool ready = false;
        (void)proto_rx_feed(rx, chunk[i], &ready);
        if (ready) {
            size_t flen = 0u;
            const uint8_t *frame = proto_rx_frame(rx, &flen);
            (void)session_handle_frame(upd, frame, flen, g_tp->send);
            handled = true;
        }
    }
    return handled;
}

static bl_status_t check_app(const flash_ops_t *ops)
{
    return boot_check_slot((const uint8_t *)BL_APP_SLOT_BASE_ADDR, ops, NULL);
}

int main(void)
{
    board_init();
    (void)g_tp->init();
    log_line("\r\nBL: start\r\n");

    const bool crc_ok = (crc_hw_init() == BL_OK);
    if (!crc_ok) {
        log_line("BL: hardware CRC self test FAILED, will not boot\r\n");
    }

    flash_ops_t ops = {flash_port_erase, flash_port_write, crc_hw_flash_crc32, NULL};
    updater_t upd;
    updater_init(&upd, &ops);
    proto_rx_t rx;
    proto_rx_init(&rx);

    /* Boot decision (see the project spec): stay if B1 is held or the app is not valid;
     * otherwise give the host HELLO_WINDOW_MS to say hello before jumping. */
    bool update_mode = true;
    if (!crc_ok) {
        log_line("BL: update mode (no CRC unit)\r\n");
    } else if (board_button_pressed()) {
        log_line("BL: button held, staying\r\n");
    } else if (check_app(&ops) != BL_OK) {
        log_line("BL: no valid app, staying\r\n");
    } else {
        const uint32_t start = board_millis();
        update_mode = false;
        while (!update_mode && ((board_millis() - start) < HELLO_WINDOW_MS)) {
            (void)pump(&upd, &rx, POLL_TIMEOUT_MS);
            update_mode = upd.have_last;
        }
        if (!update_mode) {
            log_line("BL: app valid, jumping\r\n");
            boot_jump_to_app();
        }
        log_line("BL: HELLO received\r\n");
    }

    board_led_set(BOARD_LED_YELLOW, true); /* LD2 on: update mode */
    uint32_t last_banner = board_millis();
    for (;;) {
        (void)pump(&upd, &rx, POLL_TIMEOUT_MS);
        if (updater_boot_requested(&upd)) {
            if (crc_ok && (check_app(&ops) == BL_OK)) {
                board_delay_ms(ERROR_BLINK_MS / 10u); /* let the ACK drain on the wire */
                log_line("BL: booting app\r\n");
                boot_jump_to_app();
            }
            log_line("BL: BOOT requested but app invalid\r\n");
            upd.boot_requested = false;
        }
        if (!crc_ok) {
            board_led_toggle(BOARD_LED_RED);
            board_delay_ms(ERROR_BLINK_MS);
        } else if ((updater_state(&upd) == UPD_IDLE) &&
                   ((board_millis() - last_banner) >= BANNER_PERIOD_MS)) {
            last_banner = board_millis();
            board_led_toggle(BOARD_LED_RED); /* idle heartbeat */
        }
    }
}
