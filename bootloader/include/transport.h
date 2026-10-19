/**
 * @file transport.h
 * @brief Byte transport abstraction. UART today; CAN FD (ISO-TP / UDS) can implement the same
 *        interface later without touching the updater or protocol code.
 */
#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "bl_status.h"

typedef struct {
    /** Bring the link up. */
    bl_status_t (*init)(void);
    /** Send len bytes; returns after they have left the peripheral. */
    bl_status_t (*send)(const uint8_t *buf, size_t len);
    /**
     * Receive up to maxlen bytes. Waits up to timeout_ms for the first byte, then returns
     * whatever is already available. *out_len is 0 on timeout (BL_ERR_TIMEOUT).
     */
    bl_status_t (*recv)(uint8_t *buf, size_t maxlen, size_t *out_len, uint32_t timeout_ms);
} transport_t;

#endif /* TRANSPORT_H */
