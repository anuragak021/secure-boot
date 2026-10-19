#include "flash_port.h"

#include <stdbool.h>
#include <stddef.h>

#include "flash_layout.h"
#include "stm32h7xx.h"

#define FLASH_KEY1 0x45670123u
#define FLASH_KEY2 0xCDEF89ABu
#define WORDS_PER_FLASH_WORD (BL_FLASH_WORD_BYTES / 4u)
#define BUSY_POLL_LIMIT 0x7FFFFFFFu /* generous: sector erase can take seconds at 64 MHz */

#define SR_ERROR_MASK                                                                              \
    (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR | FLASH_SR_INCERR | FLASH_SR_OPERR |     \
     FLASH_SR_RDPERR | FLASH_SR_RDSERR | FLASH_SR_DBECCERR)
#define CCR_CLEAR_ALL                                                                              \
    (FLASH_CCR_CLR_EOP | FLASH_CCR_CLR_WRPERR | FLASH_CCR_CLR_PGSERR | FLASH_CCR_CLR_STRBERR |     \
     FLASH_CCR_CLR_INCERR | FLASH_CCR_CLR_OPERR | FLASH_CCR_CLR_RDPERR | FLASH_CCR_CLR_RDSERR |    \
     FLASH_CCR_CLR_SNECCERR | FLASH_CCR_CLR_DBECCERR)

static bool in_app_area(uint32_t addr, uint32_t len)
{
    return (addr >= BL_APP_SLOT_BASE_ADDR) && (addr <= BL_FLASH_BANK1_END_ADDR) &&
           (len <= (BL_FLASH_BANK1_END_ADDR - addr));
}

static void unlock(void)
{
    if ((FLASH->CR1 & FLASH_CR_LOCK) != 0u) {
        FLASH->KEYR1 = FLASH_KEY1;
        FLASH->KEYR1 = FLASH_KEY2;
    }
}

static void lock(void)
{
    FLASH->CR1 |= FLASH_CR_LOCK;
}

/** Wait until bank 1 is idle. Returns BL_OK if no error flag is set. */
static bl_status_t wait_done(bl_status_t err)
{
    uint32_t guard = BUSY_POLL_LIMIT;
    while (((FLASH->SR1 & (FLASH_SR_BSY | FLASH_SR_QW)) != 0u) && (guard > 0u)) {
        guard--;
    }
    const uint32_t sr = FLASH->SR1;
    FLASH->CCR1 = CCR_CLEAR_ALL;
    if ((guard == 0u) || ((sr & SR_ERROR_MASK) != 0u)) {
        return err;
    }
    return BL_OK;
}

bl_status_t flash_port_erase(void *ctx, uint32_t addr, uint32_t len)
{
    (void)ctx;
    if ((len == 0u) || ((addr % BL_FLASH_SECTOR_BYTES) != 0u) || !in_app_area(addr, len)) {
        return BL_ERR_FLASH_ERASE;
    }
    const uint32_t first = (addr - BL_FLASH_BANK1_BASE_ADDR) / BL_FLASH_SECTOR_BYTES;
    const uint32_t count = (len + BL_FLASH_SECTOR_BYTES - 1u) / BL_FLASH_SECTOR_BYTES;
    if ((first + count) >
        ((BL_FLASH_BANK1_END_ADDR - BL_FLASH_BANK1_BASE_ADDR) / BL_FLASH_SECTOR_BYTES)) {
        return BL_ERR_FLASH_ERASE;
    }

    unlock();
    FLASH->CCR1 = CCR_CLEAR_ALL;
    bl_status_t st = BL_OK;
    for (uint32_t s = first; (s < (first + count)) && (st == BL_OK); s++) {
        FLASH->CR1 =
            (FLASH->CR1 & ~(FLASH_CR_SNB | FLASH_CR_PG)) | FLASH_CR_SER | (s << FLASH_CR_SNB_Pos);
        FLASH->CR1 |= FLASH_CR_START;
        st = wait_done(BL_ERR_FLASH_ERASE);
        FLASH->CR1 &= ~FLASH_CR_SER;
    }
    lock();
    return st;
}

bl_status_t flash_port_write(void *ctx, uint32_t addr, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    if ((data == NULL) || (len == 0u) || ((addr % BL_FLASH_WORD_BYTES) != 0u) ||
        ((len % BL_FLASH_WORD_BYTES) != 0u) || !in_app_area(addr, len)) {
        return BL_ERR_FLASH_WRITE;
    }

    unlock();
    FLASH->CCR1 = CCR_CLEAR_ALL;
    bl_status_t st = BL_OK;
    for (uint32_t off = 0u; (off < len) && (st == BL_OK); off += BL_FLASH_WORD_BYTES) {
        volatile uint32_t *dst = (volatile uint32_t *)(addr + off);
        FLASH->CR1 |= FLASH_CR_PG;
        /* Eight consecutive 32 bit stores fill the 256 bit flash word; the last one
         * triggers the programming. Source bytes are copied with byte reads, so the
         * input needs no alignment. */
        for (uint32_t w = 0u; w < WORDS_PER_FLASH_WORD; w++) {
            const uint8_t *p = &data[off + (w * 4u)];
            dst[w] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24);
        }
        __DSB();
        st = wait_done(BL_ERR_FLASH_WRITE);
        FLASH->CR1 &= ~FLASH_CR_PG;
    }
    lock();
    return st;
}
