#include "crc_hw_port.h"

#include <stddef.h>

#include "crc32.h"
#include "stm32h7xx.h"

#define CRC_POLY_CRC32 0x04C11DB7u
#define CRC_REV_IN_BYTE CRC_CR_REV_IN_0 /* reverse bits inside each byte */
#define CRC_SELFTEST_LEN 9u

/* POLYSIZE stays 00 (32 bit polynomial). Bytes are written with 8 bit accesses so they are consumed
 * in memory order, which with per byte input reversal and output reversal equals zlib's reflected
 * CRC32. The unit's result is zlib's value XOR 0xFFFFFFFF (no final XOR in hardware). */
static uint32_t crc_hw_bytes(const uint8_t *p, size_t len)
{
    CRC->INIT = CRC32_INIT;
    CRC->CR = CRC_REV_IN_BYTE | CRC_CR_REV_OUT | CRC_CR_RESET;
    volatile uint8_t *dr8 = (volatile uint8_t *)&CRC->DR;
    for (size_t i = 0u; i < len; i++) {
        *dr8 = p[i];
    }
    return CRC->DR ^ CRC32_XOR_OUT;
}

bl_status_t crc_hw_init(void)
{
    CRC->POL = CRC_POLY_CRC32;
    static const uint8_t vec[CRC_SELFTEST_LEN] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    if (crc_hw_bytes(vec, CRC_SELFTEST_LEN) != CRC32_CHECK_VALUE) {
        return BL_ERR_VERIFY_FAIL;
    }
    return BL_OK;
}

bl_status_t crc_hw_flash_crc32(void *ctx, uint32_t addr, uint32_t len, uint32_t *out_crc)
{
    (void)ctx;
    if (out_crc == NULL) {
        return BL_ERR_INVALID_ARG;
    }
    *out_crc = crc_hw_bytes((const uint8_t *)addr, len);
    return BL_OK;
}
