/**
 * @file flash_ops.h
 * @brief Flash operations injected into the updater (real driver on target, fake flash on host).
 */
#ifndef FLASH_OPS_H
#define FLASH_OPS_H

#include <stdint.h>

#include "bl_status.h"

typedef struct {
    /** Erase every sector overlapping [addr, addr+len). addr is sector aligned. */
    bl_status_t (*erase)(void *ctx, uint32_t addr, uint32_t len);
    /** Program len bytes at addr. addr and len are multiples of BL_FLASH_WORD_BYTES. */
    bl_status_t (*write)(void *ctx, uint32_t addr, const uint8_t *data, uint32_t len);
    /** CRC32 (final value) of len bytes of flash starting at addr, read back from flash. */
    bl_status_t (*crc32_flash)(void *ctx, uint32_t addr, uint32_t len, uint32_t *out_crc);
    void *ctx; /**< passed as first argument to every op */
} flash_ops_t;

#endif /* FLASH_OPS_H */
