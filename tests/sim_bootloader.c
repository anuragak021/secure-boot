/* Host simulation of the bootloader: the real framing, session and updater code over
 * stdin/stdout, with the fake flash. Lets the Python flasher be tested end to end.
 * Usage: sim_bootloader <flash-dump-path>   (the slot is written there on stdin EOF) */
#include <stdio.h>
#include <string.h>

#include "fake_flash.h"
#include "protocol_codec.h"
#include "session.h"
#include "updater.h"

static fake_flash_t g_flash;

static bl_status_t send_stdout(const uint8_t *buf, size_t len)
{
    return (fwrite(buf, 1u, len, stdout) == len && fflush(stdout) == 0) ? BL_OK : BL_ERR_TRANSPORT;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <flash-dump-path>\n", argv[0]);
        return 2;
    }
    fake_flash_reset(&g_flash);
    const flash_ops_t ops = fake_flash_ops(&g_flash);
    updater_t upd;
    updater_init(&upd, &ops);
    proto_rx_t rx;
    proto_rx_init(&rx);

    int c;
    while ((c = fgetc(stdin)) != EOF) {
        bool ready = false;
        (void)proto_rx_feed(&rx, (uint8_t)c, &ready);
        if (ready) {
            size_t len = 0;
            const uint8_t *frame = proto_rx_frame(&rx, &len);
            (void)session_handle_frame(&upd, frame, len, send_stdout);
        }
    }
    FILE *f = fopen(argv[1], "wb");
    if (f == NULL || fwrite(g_flash.mem, 1u, sizeof(g_flash.mem), f) != sizeof(g_flash.mem)) {
        return 1;
    }
    (void)fclose(f);
    fprintf(stderr, "boot_requested=%d state=%d\n", updater_boot_requested(&upd) ? 1 : 0,
            (int)updater_state(&upd));
    return 0;
}
