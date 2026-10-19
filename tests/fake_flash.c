#include "fake_flash.h"

#include <string.h>

#include "crc32.h"

static bl_status_t ff_erase(void *ctx, uint32_t addr, uint32_t len)
{
    fake_flash_t *f = ctx;
    f->erase_calls++;
    if (f->fail_erase != 0) {
        return BL_ERR_FLASH_ERASE;
    }
    if ((addr % BL_FLASH_SECTOR_BYTES) != 0u || addr < FAKE_FLASH_BASE) {
        return BL_ERR_FLASH_ERASE;
    }
    const uint32_t sectors = (len + BL_FLASH_SECTOR_BYTES - 1u) / BL_FLASH_SECTOR_BYTES;
    const uint32_t bytes = sectors * BL_FLASH_SECTOR_BYTES;
    if (bytes > FAKE_FLASH_SIZE - (addr - FAKE_FLASH_BASE)) {
        return BL_ERR_FLASH_ERASE;
    }
    memset(&f->mem[addr - FAKE_FLASH_BASE], 0xFF, bytes);
    f->erased_bytes += bytes;
    return BL_OK;
}

static bl_status_t ff_write(void *ctx, uint32_t addr, const uint8_t *data, uint32_t len)
{
    fake_flash_t *f = ctx;
    f->write_calls++;
    if ((f->fail_write_after >= 0) && ((int)(f->write_calls - 1u) >= f->fail_write_after)) {
        return BL_ERR_FLASH_WRITE;
    }
    if ((addr % BL_FLASH_WORD_BYTES) != 0u || (len % BL_FLASH_WORD_BYTES) != 0u ||
        addr < FAKE_FLASH_BASE || len > FAKE_FLASH_SIZE - (addr - FAKE_FLASH_BASE)) {
        return BL_ERR_FLASH_WRITE;
    }
    uint8_t *dst = &f->mem[addr - FAKE_FLASH_BASE];
    for (uint32_t i = 0; i < len; i++) {
        if (dst[i] != 0xFFu) {
            return BL_ERR_FLASH_WRITE; /* real flash: word already programmed */
        }
    }
    memcpy(dst, data, len);
    return BL_OK;
}

static bl_status_t ff_crc(void *ctx, uint32_t addr, uint32_t len, uint32_t *out)
{
    const fake_flash_t *f = ctx;
    if (addr < FAKE_FLASH_BASE || len > FAKE_FLASH_SIZE - (addr - FAKE_FLASH_BASE)) {
        return BL_ERR_INVALID_ARG;
    }
    *out = crc32_compute(&f->mem[addr - FAKE_FLASH_BASE], len);
    return BL_OK;
}

void fake_flash_reset(fake_flash_t *f)
{
    memset(f->mem, 0xFF, sizeof(f->mem));
    f->fail_erase = 0;
    f->fail_write_after = -1;
    f->erase_calls = 0;
    f->write_calls = 0;
    f->erased_bytes = 0;
}

flash_ops_t fake_flash_ops(fake_flash_t *f)
{
    flash_ops_t ops = {ff_erase, ff_write, ff_crc, f};
    return ops;
}
