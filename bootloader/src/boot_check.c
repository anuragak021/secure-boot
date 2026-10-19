#include "boot_check.h"

#include <stdbool.h>

#include "bytes.h"
#include "flash_layout.h"
#include "header.h"

#define THUMB_BIT 1u
#define VECTOR_SP_OFFSET BL_APP_HEADER_AREA_BYTES
#define VECTOR_RESET_OFFSET (BL_APP_HEADER_AREA_BYTES + 4u)

static bool sp_in_range(uint32_t sp, uint32_t base, uint32_t end)
{
    return (sp > base) && (sp <= end);
}

bl_status_t boot_check_vectors(uint32_t sp, uint32_t reset_vec, uint32_t image_bytes)
{
    const bool sp_ok = sp_in_range(sp, BL_AXI_SRAM_BASE_ADDR, BL_AXI_SRAM_END_ADDR) ||
                       sp_in_range(sp, BL_DTCM_BASE_ADDR, BL_DTCM_END_ADDR);
    if (!sp_ok || ((sp % BL_STACK_ALIGN_BYTES) != 0u)) {
        return BL_ERR_BAD_VECTORS;
    }
    if ((reset_vec & THUMB_BIT) == 0u) {
        return BL_ERR_BAD_VECTORS; /* Cortex-M can only run Thumb code */
    }
    const uint32_t entry = reset_vec & ~THUMB_BIT;
    if ((entry < BL_APP_PAYLOAD_ADDR) || (entry >= (BL_APP_PAYLOAD_ADDR + image_bytes))) {
        return BL_ERR_BAD_VECTORS;
    }
    return BL_OK;
}

bl_status_t boot_check_slot(const uint8_t *slot, const flash_ops_t *ops, image_header_t *out_hdr)
{
    image_header_t hdr;
    if ((slot == NULL) || (ops == NULL) || (ops->crc32_flash == NULL)) {
        return BL_ERR_INVALID_ARG;
    }
    if ((header_parse(slot, IMAGE_HEADER_BYTES, &hdr) != BL_OK) ||
        (header_validate(&hdr, BL_APP_PAYLOAD_MAX_BYTES) != BL_OK)) {
        return BL_ERR_BAD_HEADER;
    }
    uint32_t crc = 0u;
    if ((ops->crc32_flash(ops->ctx, BL_APP_PAYLOAD_ADDR, hdr.image_size, &crc) != BL_OK) ||
        (crc != hdr.image_crc32)) {
        return BL_ERR_VERIFY_FAIL;
    }
    const bl_status_t st = boot_check_vectors(
        le32_read(&slot[VECTOR_SP_OFFSET]), le32_read(&slot[VECTOR_RESET_OFFSET]), hdr.image_size);
    if (st != BL_OK) {
        return st;
    }
    if (out_hdr != NULL) {
        *out_hdr = hdr;
    }
    return BL_OK;
}
