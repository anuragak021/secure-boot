#include <string.h>

#include "protocol_codec.h"
#include "test_util.h"

static void test_golden_hello(void)
{
    /* raw = 01 00 0000 | crc32 = 79 b8 f8 99 (checked against Python zlib) */
    uint8_t frame[PROTO_MAX_FRAME_BYTES];
    size_t n = 0;
    CHECK_EQ(proto_encode(PROTO_CMD_HELLO, 0u, NULL, 0u, frame, sizeof(frame), &n), BL_OK);
    const uint8_t expect[] = {0x02, 0x01, 0x01, 0x01, 0x05, 0x79, 0xB8, 0xF8, 0x99, 0x00};
    CHECK_EQ(n, sizeof(expect));
    CHECK(memcmp(frame, expect, sizeof(expect)) == 0);
}

static void roundtrip(uint16_t len)
{
    uint8_t payload[PROTO_MAX_PAYLOAD_BYTES];
    for (unsigned i = 0; i < len; i++) {
        payload[i] = (uint8_t)((i % 5u == 0u) ? 0u : i); /* plenty of zeros */
    }
    uint8_t frame[PROTO_MAX_FRAME_BYTES];
    uint8_t scratch[PROTO_MAX_RAW_BYTES];
    size_t n = 0;
    proto_packet_t p;
    CHECK_EQ(proto_encode(PROTO_CMD_DATA, 0xFEu, payload, len, frame, sizeof(frame), &n), BL_OK);
    CHECK(n >= 2u);
    CHECK_EQ(frame[n - 1u], 0u);
    for (size_t i = 0; i + 1u < n; i++) {
        CHECK(frame[i] != 0u);
    }
    CHECK_EQ(proto_decode(frame, n - 1u, scratch, sizeof(scratch), &p), BL_OK);
    CHECK_EQ(p.cmd, PROTO_CMD_DATA);
    CHECK_EQ(p.seq, 0xFEu);
    CHECK_EQ(p.len, len);
    CHECK(len == 0u || memcmp(p.payload, payload, len) == 0);
}

static void test_roundtrip_sizes(void)
{
    const uint16_t sizes[] = {0, 1, 4, 28, 253, 254, 255, 256, 259, PROTO_MAX_PAYLOAD_BYTES};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        roundtrip(sizes[i]);
    }
}

static void test_payload_too_long(void)
{
    uint8_t payload[PROTO_MAX_PAYLOAD_BYTES + 1u] = {0};
    uint8_t frame[PROTO_MAX_FRAME_BYTES + 8u];
    size_t n = 0;
    CHECK_EQ(proto_encode(1, 0, payload, PROTO_MAX_PAYLOAD_BYTES + 1u, frame, sizeof(frame), &n),
             BL_ERR_BAD_LEN);
}

static size_t make_frame(uint8_t *frame, size_t max)
{
    const uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t n = 0;
    proto_encode(PROTO_CMD_START, 9u, payload, sizeof(payload), frame, max, &n);
    return n - 1u; /* without delimiter */
}

static void test_single_bit_flips_detected(void)
{
    uint8_t frame[PROTO_MAX_FRAME_BYTES];
    uint8_t scratch[PROTO_MAX_RAW_BYTES];
    const size_t n = make_frame(frame, sizeof(frame));
    proto_packet_t p;
    unsigned accepted = 0;
    for (size_t byte = 0; byte < n; byte++) {
        for (unsigned bit = 0; bit < 8u; bit++) {
            uint8_t copy[PROTO_MAX_FRAME_BYTES];
            memcpy(copy, frame, n);
            copy[byte] ^= (uint8_t)(1u << bit);
            if (proto_decode(copy, n, scratch, sizeof(scratch), &p) == BL_OK) {
                accepted++;
            }
        }
    }
    CHECK_EQ(accepted, 0u);
}

static void test_bad_crc_still_reports_seq(void)
{
    uint8_t frame[PROTO_MAX_FRAME_BYTES];
    uint8_t scratch[PROTO_MAX_RAW_BYTES];
    const size_t n = make_frame(frame, sizeof(frame));
    frame[n - 1u] ^= 0x01u; /* last CRC byte */
    proto_packet_t p;
    CHECK_EQ(proto_decode(frame, n, scratch, sizeof(scratch), &p), BL_ERR_BAD_CRC);
    CHECK_EQ(p.seq, 9u);
    CHECK_EQ(p.cmd, PROTO_CMD_START);
}

static void test_truncated_and_length_mismatch(void)
{
    uint8_t frame[PROTO_MAX_FRAME_BYTES];
    uint8_t scratch[PROTO_MAX_RAW_BYTES];
    const size_t n = make_frame(frame, sizeof(frame));
    proto_packet_t p;
    CHECK(proto_decode(frame, n - 3u, scratch, sizeof(scratch), &p) != BL_OK);
    CHECK(proto_decode(frame, 1u, scratch, sizeof(scratch), &p) != BL_OK);
}

static void test_rx_accumulator(void)
{
    proto_rx_t rx;
    proto_rx_init(&rx);
    uint8_t frame[PROTO_MAX_FRAME_BYTES];
    const size_t n = make_frame(frame, sizeof(frame));
    bool ready = false;

    CHECK_EQ(proto_rx_feed(&rx, 0u, &ready), BL_OK); /* stray delimiters are ignored */
    CHECK(!ready);
    CHECK_EQ(proto_rx_feed(&rx, 0u, &ready), BL_OK);
    CHECK(!ready);
    for (size_t i = 0; i < n; i++) {
        CHECK_EQ(proto_rx_feed(&rx, frame[i], &ready), BL_OK);
        CHECK(!ready);
    }
    CHECK_EQ(proto_rx_feed(&rx, 0u, &ready), BL_OK);
    CHECK(ready);
    size_t len = 0;
    const uint8_t *f = proto_rx_frame(&rx, &len);
    CHECK_EQ(len, n);
    CHECK(memcmp(f, frame, n) == 0);

    /* the next byte starts a fresh frame */
    CHECK_EQ(proto_rx_feed(&rx, frame[0], &ready), BL_OK);
    CHECK(!ready);
}

static void test_rx_overflow_recovers(void)
{
    proto_rx_t rx;
    proto_rx_init(&rx);
    bool ready = false;
    for (size_t i = 0; i < PROTO_MAX_FRAME_BYTES + 50u; i++) {
        CHECK_EQ(proto_rx_feed(&rx, 0x42u, &ready), BL_OK);
        CHECK(!ready);
    }
    CHECK_EQ(proto_rx_feed(&rx, 0u, &ready), BL_ERR_OVERFLOW);
    CHECK(!ready);

    uint8_t frame[PROTO_MAX_FRAME_BYTES];
    const size_t n = make_frame(frame, sizeof(frame));
    for (size_t i = 0; i < n; i++) {
        proto_rx_feed(&rx, frame[i], &ready);
    }
    proto_rx_feed(&rx, 0u, &ready);
    CHECK(ready);
}

int main(void)
{
    printf("test_protocol\n");
    RUN(test_golden_hello);
    RUN(test_roundtrip_sizes);
    RUN(test_payload_too_long);
    RUN(test_single_bit_flips_detected);
    RUN(test_bad_crc_still_reports_seq);
    RUN(test_truncated_and_length_mismatch);
    RUN(test_rx_accumulator);
    RUN(test_rx_overflow_recovers);
    TEST_SUMMARY("test_protocol");
}
