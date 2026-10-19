/**
 * @file flash_port.h
 * @brief STM32H7 bank 1 flash erase/program driver (register level) behind flash_ops_t.
 */
#ifndef FLASH_PORT_H
#define FLASH_PORT_H

#include <stdint.h>

#include "bl_status.h"

/**
 * @brief Erase every 128 KB sector of bank 1 that overlaps [addr, addr+len).
 * @param ctx  Unused.
 * @param addr Sector aligned start address inside bank 1, above the bootloader sector.
 * @param len  Bytes to cover (rounded up to whole sectors).
 * @return BL_OK, BL_ERR_FLASH_ERASE on any hardware error or out of range request.
 * @note Blocks until done; the CPU stalls on bank 1 reads meanwhile, so no ISR runs for up to
 *       a few seconds per sector.
 */
bl_status_t flash_port_erase(void *ctx, uint32_t addr, uint32_t len);

/**
 * @brief Program len bytes (multiple of 32) at a 32 byte aligned address in bank 1.
 * @param ctx  Unused.
 * @param addr Flash word aligned address, inside the app slot.
 * @param data Source bytes.
 * @param len  Multiple of BL_FLASH_WORD_BYTES.
 * @return BL_OK or BL_ERR_FLASH_WRITE.
 */
bl_status_t flash_port_write(void *ctx, uint32_t addr, const uint8_t *data, uint32_t len);

#endif /* FLASH_PORT_H */
