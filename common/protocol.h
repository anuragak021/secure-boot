/**
 * @file protocol.h
 * @brief Wire protocol constants shared by bootloader and host tools.
 *
 * Frame = COBS( cmd u8 | seq u8 | len u16 LE | payload | crc32 u32 LE over cmd..payload ) + 0x00.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define PROTO_VERSION 1u
#define BL_VERSION 0x00010000u /**< bootloader version reported in HELLO (major.minor<<16) */

#define PROTO_CMD_HELLO 0x01u
#define PROTO_CMD_START 0x02u
#define PROTO_CMD_DATA 0x03u
#define PROTO_CMD_VERIFY 0x04u
#define PROTO_CMD_BOOT 0x05u
#define PROTO_CMD_ACK 0x80u
#define PROTO_CMD_NACK 0x81u

#define PROTO_FRAME_DELIMITER 0x00u

#define PROTO_PREFIX_BYTES 4u /**< cmd + seq + len */
#define PROTO_CRC_BYTES 4u
#define PROTO_DATA_OFFSET_BYTES 4u
#define PROTO_MAX_DATA_BYTES 256u /**< image bytes per DATA packet (8 flash words) */
/** DATA payload = offset + data. This is 260, not 256: the 256 limit applies to image bytes. */
#define PROTO_MAX_PAYLOAD_BYTES (PROTO_DATA_OFFSET_BYTES + PROTO_MAX_DATA_BYTES)
#define PROTO_MAX_RAW_BYTES (PROTO_PREFIX_BYTES + PROTO_MAX_PAYLOAD_BYTES + PROTO_CRC_BYTES)

/** Worst-case COBS output for n input bytes. */
#define COBS_MAX_ENCODED_BYTES(n) ((n) + (((n) + 253u) / 254u) + 1u)
/** Encoded packet plus the trailing delimiter. */
#define PROTO_MAX_FRAME_BYTES (COBS_MAX_ENCODED_BYTES(PROTO_MAX_RAW_BYTES) + 1u)

/** Length of the HELLO reply payload: proto u8, state u8, max_data u16, bl_version u32, slot u32.
 */
#define PROTO_HELLO_REPLY_BYTES 12u

#endif /* PROTOCOL_H */
