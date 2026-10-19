#include "header.h"

#include "bytes.h"
#include "crc32.h"

#define WORD_BYTES 4u

bl_status_t header_parse(const uint8_t *buf, size_t len, image_header_t *out)
{
    if ((buf == NULL) || (out == NULL)) {
        return BL_ERR_INVALID_ARG;
    }
    if (len != IMAGE_HEADER_BYTES) {
        return BL_ERR_BAD_LEN;
    }
    out->magic = le32_read(&buf[0u * WORD_BYTES]);
    out->header_version = le32_read(&buf[1u * WORD_BYTES]);
    out->fw_version = le32_read(&buf[2u * WORD_BYTES]);
    out->image_size = le32_read(&buf[3u * WORD_BYTES]);
    out->image_crc32 = le32_read(&buf[4u * WORD_BYTES]);
    out->flags = le32_read(&buf[5u * WORD_BYTES]);
    out->header_crc32 = le32_read(&buf[6u * WORD_BYTES]);
    return BL_OK;
}

bl_status_t header_serialize(const image_header_t *hdr, uint8_t *buf)
{
    if ((hdr == NULL) || (buf == NULL)) {
        return BL_ERR_INVALID_ARG;
    }
    le32_write(&buf[0u * WORD_BYTES], hdr->magic);
    le32_write(&buf[1u * WORD_BYTES], hdr->header_version);
    le32_write(&buf[2u * WORD_BYTES], hdr->fw_version);
    le32_write(&buf[3u * WORD_BYTES], hdr->image_size);
    le32_write(&buf[4u * WORD_BYTES], hdr->image_crc32);
    le32_write(&buf[5u * WORD_BYTES], hdr->flags);
    le32_write(&buf[6u * WORD_BYTES], hdr->header_crc32);
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
