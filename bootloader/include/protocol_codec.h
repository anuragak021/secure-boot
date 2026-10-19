/**
 * @file protocol_codec.h
 * @brief Packet framing: build and parse COBS framed, CRC protected packets; byte-wise receiver.
 */
#ifndef PROTOCOL_CODEC_H
#define PROTOCOL_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bl_status.h"
#include "protocol.h"

/** A decoded packet. payload points into the scratch buffer given to proto_decode(). */
typedef struct {
    uint8_t cmd;
    uint8_t seq;
    uint16_t len;
    const uint8_t *payload;
} proto_packet_t;

/** Byte-wise frame accumulator, fed from the main loop with bytes taken from the UART ring. */
typedef struct {
    uint8_t buf[PROTO_MAX_FRAME_BYTES];
    size_t len;
    bool overflow; /**< current frame too long: drop bytes until the next delimiter */
    bool done;     /**< a frame is ready; the next feed starts a new one */
} proto_rx_t;

/**
 * @brief Build a wire frame: raw packet, CRC, COBS, trailing 0x00.
 * @param cmd       Command byte.
 * @param seq       Sequence number.
 * @param payload   Payload bytes (may be NULL only if len is 0).
 * @param len       Payload length, at most PROTO_MAX_PAYLOAD_BYTES.
 * @param frame     Output buffer.
 * @param frame_max Size of frame; PROTO_MAX_FRAME_BYTES is always enough.
 * @param frame_len Receives the frame length including the delimiter.
 * @return BL_OK, BL_ERR_BAD_LEN (payload too long), BL_ERR_OVERFLOW, BL_ERR_INVALID_ARG.
 */
bl_status_t proto_encode(uint8_t cmd, uint8_t seq, const uint8_t *payload, uint16_t len,
                         uint8_t *frame, size_t frame_max, size_t *frame_len);

/**
 * @brief Decode and verify one frame (without its trailing delimiter).
 * @param frame       COBS data.
 * @param frame_len   Length of frame.
 * @param scratch     Work buffer receiving the decoded packet, at least PROTO_MAX_RAW_BYTES.
 * @param scratch_max Size of scratch.
 * @param pkt         Receives the packet. cmd and seq are filled as soon as they are readable,
 *                    even when the CRC check then fails, so a NACK can echo the seq.
 * @return BL_OK, BL_ERR_COBS, BL_ERR_BAD_LEN, BL_ERR_BAD_CRC, BL_ERR_OVERFLOW, BL_ERR_INVALID_ARG.
 */
bl_status_t proto_decode(const uint8_t *frame, size_t frame_len, uint8_t *scratch,
                         size_t scratch_max, proto_packet_t *pkt);

/** @brief Reset the receiver to the empty state. */
void proto_rx_init(proto_rx_t *rx);

/**
 * @brief Feed one byte. Empty frames (back to back delimiters) are ignored.
 * @param rx          Receiver.
 * @param byte        Next byte from the line.
 * @param frame_ready Set true when a complete frame is available via proto_rx_frame().
 * @return BL_OK, or BL_ERR_OVERFLOW once at the delimiter that ends an over-long frame.
 */
bl_status_t proto_rx_feed(proto_rx_t *rx, uint8_t byte, bool *frame_ready);

/**
 * @brief Access the completed frame (valid until the next proto_rx_feed()).
 * @param rx  Receiver.
 * @param len Receives the frame length (without delimiter).
 * @return Pointer to the frame bytes.
 */
const uint8_t *proto_rx_frame(const proto_rx_t *rx, size_t *len);

#endif /* PROTOCOL_CODEC_H */
