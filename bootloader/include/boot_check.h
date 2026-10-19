/**
 * @file boot_check.h
 * @brief Decide whether the app slot holds a bootable image (pure logic, host testable).
 */
#ifndef BOOT_CHECK_H
#define BOOT_CHECK_H

#include <stdbool.h>
#include <stdint.h>

#include "bl_status.h"
#include "flash_ops.h"
#include "image_header.h"

/**
 * @brief Check the app's initial stack pointer and reset vector.
 * @param sp          First vector table word (initial MSP).
 * @param reset_vec   Second vector table word (reset handler address, Thumb bit set).
 * @param image_bytes Payload size from the header; the reset handler must lie inside it.
 * @return BL_OK or BL_ERR_BAD_VECTORS.
 */
bl_status_t boot_check_vectors(uint32_t sp, uint32_t reset_vec, uint32_t image_bytes);

/**
 * @brief Full check of the app slot: header, image CRC, vector table.
 * @param slot    Memory mapped start of the slot (header area), at least
 *                BL_APP_HEADER_AREA_BYTES + 8 readable bytes.
 * @param ops     Flash ops; only crc32_flash is used.
 * @param out_hdr Receives the parsed header when the slot is valid (may be NULL).
 * @return BL_OK if bootable, BL_ERR_BAD_HEADER, BL_ERR_VERIFY_FAIL or BL_ERR_BAD_VECTORS.
 */
bl_status_t boot_check_slot(const uint8_t *slot, const flash_ops_t *ops, image_header_t *out_hdr);

#endif /* BOOT_CHECK_H */
