#include <string.h>

#include "crc32.h"
#include "fake_flash.h"
#include "header.h"
#include "test_util.h"
#include "updater.h"

static fake_flash_t g_flash;
static updater_t g_upd;
static uint8_t g_image[4096];
static uint8_t g_hdr[IMAGE_HEADER_BYTES];

static void setup(void)
{
    fake_flash_reset(&g_flash);
    const flash_ops_t ops = fake_flash_ops(&g_flash);
    updater_init(&g_upd, &ops);
}

/* Deterministic pseudo-random image bytes and the matching serialised header. */
static void make_image(uint32_t size)
{
    uint32_t x = 12345u;
    for (uint32_t i = 0; i < size; i++) {
        x = x * 1664525u + 1013904223u;
        g_image[i] = (uint8_t)(x >> 24);
    }
    image_header_t h = {
        IMAGE_MAGIC, IMAGE_HEADER_VERSION, 0x00020000u, size, crc32_compute(g_image, size), 0u, 0u};
    h.header_crc32 = header_compute_crc(&h);
    header_serialize(&h, g_hdr);
}

static updater_reply_t send(uint8_t cmd, uint8_t seq, const uint8_t *payload, uint16_t len)
{
    proto_packet_t p = {cmd, seq, len, payload};
    updater_reply_t r;
    memset(&r, 0, sizeof(r));
    (void)updater_process(&g_upd, &p, &r);
    return r;
}

static bool is_ack(updater_reply_t r, uint8_t seq)
{
    return r.cmd == PROTO_CMD_ACK && r.seq == seq;
}

static bool is_nack(updater_reply_t r, uint8_t seq, bl_status_t err)
{
    return r.cmd == PROTO_CMD_NACK && r.seq == seq && r.len == 1u && r.payload[0] == (uint8_t)err;
}

static updater_reply_t send_data(uint8_t seq, uint32_t offset, uint32_t n)
{
    uint8_t pl[PROTO_MAX_PAYLOAD_BYTES];
    pl[0] = (uint8_t)offset;
    pl[1] = (uint8_t)(offset >> 8);
    pl[2] = (uint8_t)(offset >> 16);
    pl[3] = (uint8_t)(offset >> 24);
    memcpy(&pl[4], &g_image[offset], n);
    return send(PROTO_CMD_DATA, seq, pl, (uint16_t)(n + 4u));
}

/* Runs HELLO, START and all DATA packets. Returns the next free seq. */
static uint8_t upload_all(uint32_t size, uint8_t first_seq)
{
    uint8_t seq = first_seq;
    CHECK(is_ack(send(PROTO_CMD_START, seq, g_hdr, IMAGE_HEADER_BYTES), seq));
    seq++;
    for (uint32_t off = 0; off < size;) {
        const uint32_t n = (size - off > PROTO_MAX_DATA_BYTES) ? PROTO_MAX_DATA_BYTES : size - off;
        CHECK(is_ack(send_data(seq, off, n), seq));
        seq++;
        off += n;
    }
    return seq;
}

static bool slot_header_valid(void)
{
    image_header_t h;
    header_parse(&g_flash.mem[0], IMAGE_HEADER_BYTES, &h);
    return header_validate(&h, BL_APP_PAYLOAD_MAX_BYTES) == BL_OK;
}

static void test_hello(void)
{
    setup();
    const updater_reply_t r = send(PROTO_CMD_HELLO, 7, NULL, 0);
    CHECK(is_ack(r, 7));
    CHECK_EQ(r.len, PROTO_HELLO_REPLY_BYTES);
    CHECK_EQ(r.payload[0], PROTO_VERSION);
    CHECK_EQ(r.payload[1], UPD_IDLE);
    CHECK_EQ(r.payload[2] | (r.payload[3] << 8), PROTO_MAX_DATA_BYTES);
}

static void test_full_update(void)
{
    setup();
    make_image(1000);
    CHECK(is_ack(send(PROTO_CMD_HELLO, 0, NULL, 0), 0));
    uint8_t seq = upload_all(1000, 1);
    CHECK_EQ(updater_state(&g_upd), UPD_RECEIVING);
    CHECK(!slot_header_valid()); /* header is written last */
    CHECK(is_ack(send(PROTO_CMD_VERIFY, seq, NULL, 0), seq));
    seq++;
    CHECK_EQ(updater_state(&g_upd), UPD_VERIFIED);
    CHECK(slot_header_valid());
    CHECK(memcmp(&g_flash.mem[BL_APP_HEADER_AREA_BYTES], g_image, 1000) == 0);
    /* final partial word padded with 0xFF */
    for (unsigned i = 1000; i < 1024; i++) {
        CHECK_EQ(g_flash.mem[BL_APP_HEADER_AREA_BYTES + i], 0xFFu);
    }
    CHECK(!updater_boot_requested(&g_upd));
    CHECK(is_ack(send(PROTO_CMD_BOOT, seq, NULL, 0), seq));
    CHECK(updater_boot_requested(&g_upd));
}

static void test_erase_only_needed_sectors(void)
{
    setup();
    make_image(1000);
    send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES);
    CHECK_EQ(g_flash.erased_bytes, BL_FLASH_SECTOR_BYTES);
}

static void test_verify_fail_when_data_differs_from_header(void)
{
    setup();
    make_image(1000);
    g_image[500] ^= 0x01u; /* corrupt the payload that gets sent; g_hdr still has the clean CRC */
    uint8_t seq = upload_all(1000, 0);
    CHECK(is_nack(send(PROTO_CMD_VERIFY, seq, NULL, 0), seq, BL_ERR_VERIFY_FAIL));
    CHECK_EQ(updater_state(&g_upd), UPD_IDLE);
    CHECK(!slot_header_valid());
    CHECK(is_nack(send(PROTO_CMD_BOOT, seq, NULL, 0), seq, BL_ERR_NOT_READY));
}

static void test_bad_headers_do_not_erase(void)
{
    setup();
    make_image(1000);
    memset(&g_flash.mem[0x2000], 0xA5, 64); /* marker standing in for the old app */
    uint8_t bad[IMAGE_HEADER_BYTES];

    memcpy(bad, g_hdr, sizeof(bad));
    bad[0] ^= 0x01u; /* wrong magic */
    CHECK(is_nack(send(PROTO_CMD_START, 0, bad, sizeof(bad)), 0, BL_ERR_BAD_HEADER));

    memcpy(bad, g_hdr, sizeof(bad));
    bad[24] ^= 0x01u; /* header CRC wrong */
    CHECK(is_nack(send(PROTO_CMD_START, 0, bad, sizeof(bad)), 0, BL_ERR_BAD_HEADER));

    memcpy(bad, g_hdr, sizeof(bad));
    bad[13] ^= 0x10u; /* image size changed, header CRC no longer matches */
    CHECK(is_nack(send(PROTO_CMD_START, 0, bad, sizeof(bad)), 0, BL_ERR_BAD_HEADER));

    image_header_t big = {
        IMAGE_MAGIC, IMAGE_HEADER_VERSION, 1u, BL_APP_PAYLOAD_MAX_BYTES + 1u, 0u, 0u, 0u};
    big.header_crc32 = header_compute_crc(&big);
    header_serialize(&big, bad);
    CHECK(is_nack(send(PROTO_CMD_START, 0, bad, sizeof(bad)), 0, BL_ERR_BAD_HEADER));

    CHECK(is_nack(send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES - 1u), 0, BL_ERR_BAD_LEN));

    CHECK_EQ(g_flash.erase_calls, 0u);
    CHECK_EQ(g_flash.mem[0x2000], 0xA5u);
    CHECK_EQ(updater_state(&g_upd), UPD_IDLE);
}

static void test_data_before_start(void)
{
    setup();
    make_image(100);
    CHECK(is_nack(send_data(0, 0, 32), 0, BL_ERR_NOT_READY));
    CHECK(is_nack(send(PROTO_CMD_VERIFY, 0, NULL, 0), 0, BL_ERR_NOT_READY));
}

static void test_bad_offsets_and_lengths(void)
{
    setup();
    make_image(1000);
    send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES);
    CHECK(is_nack(send_data(1, 32, 32), 1, BL_ERR_BAD_OFFSET)); /* skipped ahead */
    CHECK(is_ack(send_data(1, 0, 256), 1));                     /* same seq retried: ok */
    CHECK(is_nack(send_data(2, 0, 32), 2, BL_ERR_BAD_OFFSET));  /* going backwards */
    uint8_t only_offset[4] = {0, 1, 0, 0};
    CHECK(is_nack(send(PROTO_CMD_DATA, 2, only_offset, 4), 2, BL_ERR_BAD_LEN)); /* no data */
    CHECK(is_nack(send(PROTO_CMD_DATA, 2, only_offset, 2), 2, BL_ERR_BAD_LEN));
    CHECK(is_nack(send(PROTO_CMD_VERIFY, 2, NULL, 0), 2, BL_ERR_NOT_READY)); /* incomplete */
}

static void test_data_beyond_image_end(void)
{
    setup();
    make_image(100);
    send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES);
    CHECK(is_nack(send_data(1, 0, 128), 1, BL_ERR_BAD_OFFSET)); /* overshoots the declared size */
    CHECK(is_ack(send_data(1, 0, 100), 1));
    CHECK(is_nack(send_data(2, 100, 32), 2, BL_ERR_BAD_OFFSET)); /* image already complete */
}

static void test_duplicate_packet_not_written_twice(void)
{
    setup();
    make_image(1000);
    send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES);
    CHECK(is_ack(send_data(1, 0, 256), 1));
    const unsigned writes = g_flash.write_calls;
    CHECK(is_ack(send_data(1, 0, 256), 1)); /* ACK was lost, host resends */
    CHECK_EQ(g_flash.write_calls, writes);
    CHECK(is_ack(send_data(2, 256, 256), 2));
}

static void test_duplicate_start_does_not_reerase(void)
{
    setup();
    make_image(1000);
    send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES);
    const unsigned erases = g_flash.erase_calls;
    CHECK(is_ack(send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES), 0)); /* ACK was lost */
    CHECK_EQ(g_flash.erase_calls, erases);
    CHECK(is_ack(send_data(1, 0, 256), 1));
    /* a duplicate that is no longer the last packet is a sequence error, not a restart */
    CHECK(is_nack(send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES), 0, BL_ERR_BAD_SEQ));
}

static void test_wrong_seq(void)
{
    setup();
    make_image(1000);
    send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES);
    CHECK(is_nack(send_data(5, 0, 256), 5, BL_ERR_BAD_SEQ));
    CHECK(is_ack(send_data(1, 0, 256), 1));
}

static void test_seq_wraps(void)
{
    setup();
    make_image(4000);
    send(PROTO_CMD_START, 250, g_hdr, IMAGE_HEADER_BYTES);
    uint8_t seq = 251;
    for (uint32_t off = 0; off < 4000;) {
        const uint32_t n = (4000 - off > 256) ? 256 : 4000 - off;
        CHECK(is_ack(send_data(seq, off, n), seq));
        seq++;
        off += n;
    }
    CHECK(is_ack(send(PROTO_CMD_VERIFY, seq, NULL, 0), seq));
}

static void test_restart_mid_transfer(void)
{
    setup();
    make_image(1000);
    uint8_t seq = 0;
    send(PROTO_CMD_START, seq++, g_hdr, IMAGE_HEADER_BYTES);
    send_data(seq++, 0, 256);
    upload_all(1000, seq); /* START again from the top, then everything */
    seq = (uint8_t)(seq + 1u + 4u);
    CHECK(is_ack(send(PROTO_CMD_VERIFY, seq, NULL, 0), seq));
    CHECK(memcmp(&g_flash.mem[BL_APP_HEADER_AREA_BYTES], g_image, 1000) == 0);
}

static void test_killed_transfer_never_valid(void)
{
    setup();
    make_image(3000);
    send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES);
    uint8_t seq = 1;
    for (uint32_t off = 0; off < 1536; off += 256) {
        send_data(seq++, off, 256);
    }
    /* power cut here: a fresh updater / boot check sees the slot */
    CHECK(!slot_header_valid());
    CHECK_EQ(updater_state(&g_upd), UPD_RECEIVING);
}

static void test_flash_faults(void)
{
    setup();
    make_image(1000);
    g_flash.fail_erase = 1;
    CHECK(is_nack(send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES), 0, BL_ERR_FLASH_ERASE));
    CHECK_EQ(updater_state(&g_upd), UPD_IDLE);

    setup();
    send(PROTO_CMD_START, 0, g_hdr, IMAGE_HEADER_BYTES);
    g_flash.fail_write_after = 2;
    CHECK(is_ack(send_data(1, 0, 64), 1)); /* 2 words fine */
    CHECK(is_nack(send_data(2, 64, 64), 2, BL_ERR_FLASH_WRITE));
    CHECK_EQ(updater_state(&g_upd), UPD_IDLE);
    CHECK(is_nack(send_data(2, 64, 64), 2, BL_ERR_NOT_READY));
}

static void test_header_write_fault(void)
{
    setup();
    make_image(64);
    uint8_t seq = upload_all(64, 0);
    g_flash.fail_write_after = (int)g_flash.write_calls; /* next write (the header) fails */
    CHECK(is_nack(send(PROTO_CMD_VERIFY, seq, NULL, 0), seq, BL_ERR_FLASH_WRITE));
    CHECK(!slot_header_valid());
}

static void test_boot_before_verified_and_unknown_cmd(void)
{
    setup();
    CHECK(is_nack(send(PROTO_CMD_BOOT, 0, NULL, 0), 0, BL_ERR_NOT_READY));
    CHECK(!updater_boot_requested(&g_upd));
    CHECK(is_nack(send(0x7Eu, 0, NULL, 0), 0, BL_ERR_BAD_CMD));
}

static void test_second_update_after_verified(void)
{
    setup();
    make_image(1000);
    uint8_t seq = upload_all(1000, 0);
    send(PROTO_CMD_VERIFY, seq++, NULL, 0);
    CHECK_EQ(updater_state(&g_upd), UPD_VERIFIED);
    make_image(2000);
    seq = upload_all(2000, seq);
    CHECK(!slot_header_valid()); /* new START erased the old header */
    CHECK(is_ack(send(PROTO_CMD_VERIFY, seq, NULL, 0), seq));
    CHECK(slot_header_valid());
}

static void test_two_sector_image(void)
{
    setup();
    static uint8_t big_hdr[IMAGE_HEADER_BYTES];
    image_header_t h = {IMAGE_MAGIC, IMAGE_HEADER_VERSION, 1u, BL_FLASH_SECTOR_BYTES, 0u, 0u, 0u};
    h.header_crc32 = header_compute_crc(&h);
    header_serialize(&h, big_hdr);
    send(PROTO_CMD_START, 0, big_hdr, IMAGE_HEADER_BYTES);
    CHECK_EQ(g_flash.erased_bytes, 2u * BL_FLASH_SECTOR_BYTES); /* 0x400 + 128K spills over */
}

int main(void)
{
    printf("test_updater\n");
    RUN(test_hello);
    RUN(test_full_update);
    RUN(test_erase_only_needed_sectors);
    RUN(test_verify_fail_when_data_differs_from_header);
    RUN(test_bad_headers_do_not_erase);
    RUN(test_data_before_start);
    RUN(test_bad_offsets_and_lengths);
    RUN(test_data_beyond_image_end);
    RUN(test_duplicate_packet_not_written_twice);
    RUN(test_duplicate_start_does_not_reerase);
    RUN(test_wrong_seq);
    RUN(test_seq_wraps);
    RUN(test_restart_mid_transfer);
    RUN(test_killed_transfer_never_valid);
    RUN(test_flash_faults);
    RUN(test_header_write_fault);
    RUN(test_boot_before_verified_and_unknown_cmd);
    RUN(test_second_update_after_verified);
    RUN(test_two_sector_image);
    TEST_SUMMARY("test_updater");
}
