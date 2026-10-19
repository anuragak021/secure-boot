/* In-memory flash model of the app slot (0x08020000..0x08100000) with H7 rules:
 * erase per 128 KB sector to 0xFF, program per 32 byte word, no reprogramming of a word. */
#ifndef FAKE_FLASH_H
#define FAKE_FLASH_H

#include <stdint.h>

#include "flash_layout.h"
#include "flash_ops.h"

#define FAKE_FLASH_BASE BL_APP_SLOT_BASE_ADDR
#define FAKE_FLASH_SIZE (BL_FLASH_BANK1_END_ADDR - BL_APP_SLOT_BASE_ADDR)

typedef struct {
    uint8_t mem[FAKE_FLASH_SIZE];
    int fail_erase;       /**< non-zero: next erase fails */
    int fail_write_after; /**< >= 0: that many writes succeed, then every write fails */
    unsigned erase_calls;
    unsigned write_calls;
    uint32_t erased_bytes; /**< total bytes erased (sector rounded) */
} fake_flash_t;

/** Fill with 0xFF and reset counters/fault injection. */
void fake_flash_reset(fake_flash_t *f);
/** Build ops that act on f. */
flash_ops_t fake_flash_ops(fake_flash_t *f);

#endif /* FAKE_FLASH_H */
