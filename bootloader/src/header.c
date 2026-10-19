#include "header.h"

#include "bytes.h"
#include "crc32.h"

/* Byte offsets of the seven little endian u32 fields. */
#define OFF_MAGIC 0u
#define OFF_VERSION 4u
#define OFF_FW_VERSION 8u
#define OFF_IMAGE_SIZE 12u
#define OFF_IMAGE_CRC 16u
#define OFF_FLAGS 20u
#define OFF_HEADER_CRC 24u

bl_status_t header_parse(const uint8_t *buf, size_t len, image_header_t *out)
{
    if ((buf == NULL) || (out == NULL)) {
        return BL_ERR_INVALID_ARG;
    }
    if (len != IMAGE_HEADER_BYTES) {
        return BL_ERR_BAD_LEN;
    }
    out->magic = le32_read(&buf[OFF_MAGIC]);
    out->header_version = le32_read(&buf[OFF_VERSION]);
    out->fw_version = le32_read(&buf[OFF_FW_VERSION]);
    out->image_size = le32_read(&buf[OFF_IMAGE_SIZE]);
    out->image_crc32 = le32_read(&buf[OFF_IMAGE_CRC]);
    out->flags = le32_read(&buf[OFF_FLAGS]);
    out->header_crc32 = le32_read(&buf[OFF_HEADER_CRC]);
    return BL_OK;
}

bl_status_t header_serialize(const image_header_t *hdr, uint8_t *buf)
{
    if ((hdr == NULL) || (buf == NULL)) {
        return BL_ERR_INVALID_ARG;
    }
    le32_write(&buf[OFF_MAGIC], hdr->magic);
    le32_write(&buf[OFF_VERSION], hdr->header_version);
    le32_write(&buf[OFF_FW_VERSION], hdr->fw_version);
    le32_write(&buf[OFF_IMAGE_SIZE], hdr->image_size);
    le32_write(&buf[OFF_IMAGE_CRC], hdr->image_crc32);
    le32_write(&buf[OFF_FLAGS], hdr->flags);
    le32_write(&buf[OFF_HEADER_CRC], hdr->header_crc32);
    return BL_OK;
}

uint32_t header_compute_crc(const image_header_t *hdr)
{
    uint8_t buf[IMAGE_HEADER_BYTES];
    (void)header_serialize(hdr, buf);
    return crc32_compute(buf, IMAGE_HEADER_CRC_SPAN_BYTES);
}

bl_status_t header_validate(const image_header_t *hdr, uint32_t max_image_bytes)
{
    if (hdr == NULL) {
        return BL_ERR_INVALID_ARG;
    }
    if ((hdr->magic != IMAGE_MAGIC) || (hdr->header_version != IMAGE_HEADER_VERSION) ||
        (hdr->flags != 0u)) {
        return BL_ERR_BAD_HEADER;
    }
    if (header_compute_crc(hdr) != hdr->header_crc32) {
        return BL_ERR_BAD_HEADER;
    }
    if ((hdr->image_size == 0u) || (hdr->image_size > max_image_bytes)) {
        return BL_ERR_BAD_HEADER;
    }
    return BL_OK;
}
