/**
 * @file crc_hw_port.h
 * @brief STM32 hardware CRC unit configured to produce the same CRC32 as zlib / the host tools.
 */
#ifndef CRC_HW_PORT_H
#define CRC_HW_PORT_H

#include <stdint.h>

#include "bl_status.h"

/**
 * @brief Configure the CRC unit (poly 0x04C11DB7, init 0xFFFFFFFF, input and output bit reversal)
 *        and run a self test on "123456789".
 * @return BL_OK, or BL_ERR_VERIFY_FAIL if the unit does not give 0xCBF43926 (never trust it then).
 */
bl_status_t crc_hw_init(void);

/**
 * @brief CRC32 of memory mapped bytes, computed in one pass.
 * @param ctx     Unused (matches flash_ops_t.crc32_flash).
 * @param addr    Start address (flash is memory mapped).
 * @param len     Number of bytes.
 * @param out_crc Receives the final CRC32.
 * @return BL_OK or BL_ERR_INVALID_ARG.
 * @note Uses the shared hardware unit and resets it; do not call concurrently.
 */
bl_status_t crc_hw_flash_crc32(void *ctx, uint32_t addr, uint32_t len, uint32_t *out_crc);

#endif /* CRC_HW_PORT_H */
