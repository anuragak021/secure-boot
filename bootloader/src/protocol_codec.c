#include "protocol_codec.h"

#include <string.h>

#include "bytes.h"
#include "cobs.h"
#include "crc32.h"

#define OFFSET_CMD 0u
#define OFFSET_SEQ 1u
#define OFFSET_LEN 2u
#define OFFSET_PAYLOAD PROTO_PREFIX_BYTES

bl_status_t proto_encode(uint8_t cmd, uint8_t seq, const uint8_t *payload, uint16_t len,
                         uint8_t *frame, size_t frame_max, size_t *frame_len)
{
    if ((frame == NULL) || (frame_len == NULL) || ((payload == NULL) && (len != 0u))) {
        return BL_ERR_INVALID_ARG;
    }
    if (len > PROTO_MAX_PAYLOAD_BYTES) {
        return BL_ERR_BAD_LEN;
    }
    uint8_t raw[PROTO_MAX_RAW_BYTES];
    raw[OFFSET_CMD] = cmd;
    raw[OFFSET_SEQ] = seq;
    le16_write(&raw[OFFSET_LEN], len);
    if (len != 0u) {
        memcpy(&raw[OFFSET_PAYLOAD], payload, len);
    }
    const size_t body = (size_t)OFFSET_PAYLOAD + len;
    le32_write(&raw[body], crc32_compute(raw, body));

    size_t enc_len = 0u;
    if (frame_max == 0u) {
        return BL_ERR_OVERFLOW;
    }
    /* Reserve one byte for the delimiter. */
    const bl_status_t st =
        cobs_encode(raw, body + PROTO_CRC_BYTES, frame, frame_max - 1u, &enc_len);
    if (st != BL_OK) {
        return st;
    }
    frame[enc_len] = PROTO_FRAME_DELIMITER;
    *frame_len = enc_len + 1u;
    return BL_OK;
}

bl_status_t proto_decode(const uint8_t *frame, size_t frame_len, uint8_t *scratch,
                         size_t scratch_max, proto_packet_t *pkt)
{
    if ((frame == NULL) || (scratch == NULL) || (pkt == NULL)) {
        return BL_ERR_INVALID_ARG;
    }
    size_t raw_len = 0u;
    bl_status_t st = cobs_decode(frame, frame_len, scratch, scratch_max, &raw_len);
    if (st != BL_OK) {
        return st;
    }
    if (raw_len < (PROTO_PREFIX_BYTES + PROTO_CRC_BYTES)) {
        return BL_ERR_BAD_LEN;
    }
    pkt->cmd = scratch[OFFSET_CMD];
    pkt->seq = scratch[OFFSET_SEQ];
    pkt->len = le16_read(&scratch[OFFSET_LEN]);
    pkt->payload = &scratch[OFFSET_PAYLOAD];
    if ((pkt->len > PROTO_MAX_PAYLOAD_BYTES) ||
        (raw_len != ((size_t)PROTO_PREFIX_BYTES + pkt->len + PROTO_CRC_BYTES))) {
        return BL_ERR_BAD_LEN;
    }
    const size_t body = (size_t)PROTO_PREFIX_BYTES + pkt->len;
    if (le32_read(&scratch[body]) != crc32_compute(scratch, body)) {
        return BL_ERR_BAD_CRC;
    }
    return BL_OK;
}

void proto_rx_init(proto_rx_t *rx)
{
    rx->len = 0u;
    rx->overflow = false;
    rx->done = false;
}

bl_status_t proto_rx_feed(proto_rx_t *rx, uint8_t byte, bool *frame_ready)
{
    *frame_ready = false;
    if (rx->done) {
        proto_rx_init(rx);
    }
    if (byte == PROTO_FRAME_DELIMITER) {
        if (rx->overflow) {
            proto_rx_init(rx);
            return BL_ERR_OVERFLOW;
        }
        if (rx->len != 0u) {
            rx->done = true;
            *frame_ready = true;
        }
        return BL_OK;
    }
    if (rx->overflow) {
        return BL_OK;
    }
    if (rx->len >= sizeof(rx->buf)) {
        rx->overflow = true;
        return BL_OK;
    }
    rx->buf[rx->len] = byte;
    rx->len++;
    return BL_OK;
}

const uint8_t *proto_rx_frame(const proto_rx_t *rx, size_t *len)
{
    *len = rx->len;
    return rx->buf;
}
