#include "crc32.h"

/* Nibble table: 16 entries instead of 256, trading speed for 960 bytes less flash. */
#define NIBBLE_BITS 4u
#define NIBBLE_MASK 0x0Fu

static const uint32_t k_crc_nibble[16] = {0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
                                          0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
                                          0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
                                          0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};

uint32_t crc32_update(uint32_t state, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        state ^= data[i];
        state = (state >> NIBBLE_BITS) ^ k_crc_nibble[state & NIBBLE_MASK];
        state = (state >> NIBBLE_BITS) ^ k_crc_nibble[state & NIBBLE_MASK];
    }
    return state;
}

uint32_t crc32_compute(const uint8_t *data, size_t len)
{
    return crc32_update(CRC32_INIT, data, len) ^ CRC32_XOR_OUT;
}
