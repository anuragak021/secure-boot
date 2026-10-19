/**
 * @file crc32.h
 * @brief Software CRC-32 (IEEE 802.3, same as zlib/Python binascii.crc32).
 *
 * Polynomial 0xEDB88320 reflected, init 0xFFFFFFFF, final XOR 0xFFFFFFFF.
 * Check value: crc32("123456789") == 0xCBF43926.
 */
#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

#define CRC32_INIT 0xFFFFFFFFu
#define CRC32_XOR_OUT 0xFFFFFFFFu
#define CRC32_CHECK_VALUE 0xCBF43926u /* crc32 of the ASCII string "123456789" */

/**
 * @brief Feed bytes into a running CRC state.
 * @param state Running state (start with CRC32_INIT).
 * @param data  Bytes to add (may be NULL only if len is 0).
 * @param len   Number of bytes.
 * @return New running state (not yet XORed with CRC32_XOR_OUT).
 */
uint32_t crc32_update(uint32_t state, const uint8_t *data, size_t len);

/**
 * @brief One-shot CRC of a buffer.
 * @param data Bytes (may be NULL only if len is 0).
 * @param len  Number of bytes.
 * @return Final CRC32.
 */
uint32_t crc32_compute(const uint8_t *data, size_t len);

#endif /* CRC32_H */
