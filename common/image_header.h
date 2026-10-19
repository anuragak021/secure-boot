/**
 * @file image_header.h
 * @brief Firmware image header. Mirrored in tools/mkimage.py (keep both in sync).
 *
 * On flash and on the wire the header is 7 little-endian u32 words (28 bytes), serialised
 * explicitly by header.c, so the in-memory struct layout never matters.
 */
#ifndef IMAGE_HEADER_H
#define IMAGE_HEADER_H

#include <stdint.h>

#define IMAGE_MAGIC 0x544F4253u /* bytes "SBOT" on the wire (little endian) */
#define IMAGE_HEADER_VERSION 1u
#define IMAGE_HEADER_BYTES 28u          /**< serialised size */
#define IMAGE_HEADER_CRC_SPAN_BYTES 24u /**< header_crc32 covers the 6 words before it */

typedef struct {
    uint32_t magic;          /**< IMAGE_MAGIC */
    uint32_t header_version; /**< layout version of this struct */
    uint32_t fw_version;     /**< application version */
    uint32_t image_size;     /**< bytes of payload after the 0x400 header area */
    uint32_t image_crc32;    /**< CRC32 of the payload */
    uint32_t flags;          /**< reserved, 0 */
    uint32_t header_crc32;   /**< CRC32 of all fields above */
} image_header_t;

_Static_assert(sizeof(image_header_t) == IMAGE_HEADER_BYTES, "image_header_t must be 28 bytes");

#endif /* IMAGE_HEADER_H */
