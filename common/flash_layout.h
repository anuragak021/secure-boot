/**
 * @file flash_layout.h
 * @brief STM32H753ZI flash geometry and bootloader/app slot addresses.
 */
#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

#include <stdint.h>

#define BL_FLASH_WORD_BYTES 32u        /**< smallest programmable unit (256 bit + ECC) */
#define BL_FLASH_SECTOR_BYTES 0x20000u /**< 128 KB erase unit */
#define BL_FLASH_BANK1_BASE_ADDR 0x08000000u
#define BL_FLASH_BANK1_END_ADDR 0x08100000u /**< first address after bank 1 */

#define BL_BOOTLOADER_BASE_ADDR 0x08000000u
#define BL_BOOTLOADER_SIZE_BYTES BL_FLASH_SECTOR_BYTES

#define BL_APP_SLOT_BASE_ADDR 0x08020000u /**< image header area starts here */
#define BL_APP_HEADER_AREA_BYTES 0x400u   /**< reserved for header (+ future signature) */
#define BL_APP_PAYLOAD_ADDR (BL_APP_SLOT_BASE_ADDR + BL_APP_HEADER_AREA_BYTES)
#define BL_APP_PAYLOAD_MAX_BYTES (BL_FLASH_BANK1_END_ADDR - BL_APP_PAYLOAD_ADDR)

/** SRAM ranges an app's initial stack pointer may point into (the top may equal the end). */
#define BL_AXI_SRAM_BASE_ADDR 0x24000000u
#define BL_AXI_SRAM_END_ADDR 0x24080000u
#define BL_DTCM_BASE_ADDR 0x20000000u
#define BL_DTCM_END_ADDR 0x20020000u
#define BL_STACK_ALIGN_BYTES 8u /* AAPCS: the stack is 8 byte aligned */

_Static_assert(BL_APP_SLOT_BASE_ADDR % BL_FLASH_SECTOR_BYTES == 0u, "slot must be sector aligned");
_Static_assert(BL_APP_PAYLOAD_ADDR % BL_FLASH_WORD_BYTES == 0u,
               "payload must be flash-word aligned");
_Static_assert(BL_APP_PAYLOAD_ADDR % 1024u == 0u,
               "vector table needs 1 KB alignment (166 vectors)");
_Static_assert(BL_APP_HEADER_AREA_BYTES + BL_APP_PAYLOAD_MAX_BYTES ==
                   BL_FLASH_BANK1_END_ADDR - BL_APP_SLOT_BASE_ADDR,
               "slot arithmetic");

#endif /* FLASH_LAYOUT_H */
