/**
 * @file cobs.h
 * @brief Consistent Overhead Byte Stuffing. Output never contains 0x00, so 0x00 can delimit frames.
 */
#ifndef COBS_H
#define COBS_H

#include <stddef.h>
#include <stdint.h>

#include "bl_status.h"

/**
 * @brief COBS-encode a buffer (the 0x00 frame delimiter is NOT appended).
 * @param in      Input bytes (may be NULL only if in_len is 0).
 * @param in_len  Input length.
 * @param out     Output buffer.
 * @param out_max Size of out. COBS_MAX_ENCODED_BYTES(in_len) is always enough.
 * @param out_len Receives the encoded length.
 * @return BL_OK, BL_ERR_INVALID_ARG on NULL, BL_ERR_OVERFLOW if out is too small.
 */
bl_status_t cobs_encode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max,
                        size_t *out_len);

/**
 * @brief Decode a COBS buffer (without the trailing delimiter).
 * @param in      Encoded bytes.
 * @param in_len  Encoded length.
 * @param out     Output buffer (decoded size is always smaller than in_len).
 * @param out_max Size of out.
 * @param out_len Receives the decoded length.
 * @return BL_OK, BL_ERR_INVALID_ARG, BL_ERR_COBS on a zero byte or truncated block,
 *         BL_ERR_OVERFLOW if out is too small.
 */
bl_status_t cobs_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max,
                        size_t *out_len);

#endif /* COBS_H */
