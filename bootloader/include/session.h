/**
 * @file session.h
 * @brief Glue between a received frame, the updater and the reply (pure logic, host testable).
 */
#ifndef SESSION_H
#define SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "bl_status.h"
#include "updater.h"

/** Callback that transmits raw bytes (a complete reply frame). */
typedef bl_status_t (*session_send_fn)(const uint8_t *buf, size_t len);

/**
 * @brief Decode one received frame, run it through the updater and send the reply.
 *
 * Frames that fail COBS or length checks are dropped silently (the host retries on timeout).
 * A packet CRC failure is answered with NACK(BAD_CRC), because the seq is still readable.
 *
 * @param upd   Updater state machine.
 * @param frame Frame bytes without the trailing delimiter.
 * @param len   Frame length.
 * @param send  Used to transmit the reply frame.
 * @return BL_OK if the packet was executed, otherwise the reason it was not / was rejected.
 */
bl_status_t session_handle_frame(updater_t *upd, const uint8_t *frame, size_t len,
                                 session_send_fn send);

#endif /* SESSION_H */
