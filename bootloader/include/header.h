/**
 * @file header.h
 * @brief Parse, serialise and validate the firmware image header.
 */
#ifndef HEADER_H
#define HEADER_H

#include <stddef.h>
#include <stdint.h>

#include "bl_status.h"
#include "image_header.h"

/**
 * @brief Decode the 28 serialised header bytes. Does no validation.
 * @param buf Serialised header.
 * @param len Must equal IMAGE_HEADER_BYTES.
 * @param out Receives the decoded fields.
 * @return BL_OK, BL_ERR_INVALID_ARG on NULL, BL_ERR_BAD_LEN if len is wrong.
 */
bl_status_t header_parse(const uint8_t *buf, size_t len, image_header_t *out);

/**
 * @brief Encode a header into its 28 byte wire/flash form (header_crc32 is copied, not computed).
 * @param hdr Header to encode.
 * @param buf Output, at least IMAGE_HEADER_BYTES long.
 * @return BL_OK or BL_ERR_INVALID_ARG.
 */
bl_status_t header_serialize(const image_header_t *hdr, uint8_t *buf);

/**
 * @brief Compute the header CRC (CRC32 over the first 24 serialised bytes).
 * @param hdr Header whose first six fields are used.
 * @return CRC32 value to store in header_crc32.
 */
uint32_t header_compute_crc(const image_header_t *hdr);

/**
 * @brief Check magic, version, flags, header CRC and that the size fits the slot.
 * @param hdr             Parsed header.
 * @param max_image_bytes Largest payload the slot can hold.
 * @return BL_OK or BL_ERR_BAD_HEADER (or BL_ERR_INVALID_ARG on NULL).
 */
bl_status_t header_validate(const image_header_t *hdr, uint32_t max_image_bytes);

#endif /* HEADER_H */
