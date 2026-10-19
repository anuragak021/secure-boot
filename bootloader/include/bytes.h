/**
 * @file bytes.h
 * @brief Little-endian byte helpers (endianness independent, no unaligned access).
 */
#ifndef BYTES_H
#define BYTES_H

#include <stdint.h>

/** @brief Read a little-endian u16 from p[0..1]. */
static inline uint16_t le16_read(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

/** @brief Read a little-endian u32 from p[0..3]. */
static inline uint32_t le32_read(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/** @brief Write v as little-endian u16 to p[0..1]. */
static inline void le16_write(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

/** @brief Write v as little-endian u32 to p[0..3]. */
static inline void le32_write(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)(v >> 24);
}

#endif /* BYTES_H */
