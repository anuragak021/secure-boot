/**
 * @file updater.h
 * @brief Firmware update state machine (IDLE -> RECEIVING -> VERIFIED). Pure logic: all flash
 *        access goes through injected flash_ops_t, so it runs unchanged on the host.
 */
#ifndef UPDATER_H
#define UPDATER_H

#include <stdbool.h>
#include <stdint.h>

#include "bl_status.h"
#include "flash_ops.h"
#include "flash_layout.h"
#include "image_header.h"
#include "protocol_codec.h"

#define UPDATER_REPLY_MAX_PAYLOAD_BYTES PROTO_HELLO_REPLY_BYTES

typedef enum { UPD_IDLE = 0, UPD_RECEIVING = 1, UPD_VERIFIED = 2 } updater_state_t;

/** Reply to send for one request: an ACK (maybe with payload) or a NACK with one error byte. */
typedef struct {
    uint8_t cmd; /**< PROTO_CMD_ACK or PROTO_CMD_NACK */
    uint8_t seq; /**< echoes the request seq */
    uint16_t len;
    uint8_t payload[UPDATER_REPLY_MAX_PAYLOAD_BYTES];
} updater_reply_t;

typedef struct {
    flash_ops_t ops;
    updater_state_t state;
    bool have_last; /**< at least one packet has been accepted */
    bool boot_requested;
    uint8_t last_seq; /**< seq of the last successfully handled packet */
    uint8_t last_cmd;
    image_header_t header;   /**< header received in START, written to flash at VERIFY */
    uint32_t received_bytes; /**< image bytes accepted so far (next expected DATA offset) */
    uint32_t flushed_bytes;  /**< image bytes already programmed to flash */
    uint32_t word_fill;      /**< bytes waiting in word_buf */
    uint8_t word_buf[BL_FLASH_WORD_BYTES];
} updater_t;

/**
 * @brief Initialise the updater in IDLE state.
 * @param u   Updater.
 * @param ops Flash operations (copied).
 */
void updater_init(updater_t *u, const flash_ops_t *ops);

/**
 * @brief Handle one verified packet and build the reply.
 * @param u     Updater.
 * @param req   Packet that passed framing and CRC checks.
 * @param reply Always filled (ACK or NACK).
 * @return BL_OK if the command succeeded, otherwise the error that went into the NACK.
 * @note Side effects: may erase and program flash through the injected ops.
 */
bl_status_t updater_process(updater_t *u, const proto_packet_t *req, updater_reply_t *reply);

/** @brief Current state. */
updater_state_t updater_state(const updater_t *u);

/** @brief True once a BOOT command was accepted in VERIFIED state. */
bool updater_boot_requested(const updater_t *u);

#endif /* UPDATER_H */
