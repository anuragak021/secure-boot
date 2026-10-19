#include <string.h>

#include "crc32.h"
#include "flash_layout.h"
#include "header.h"
#include "test_util.h"

static image_header_t good(void)
{
    image_header_t h = {IMAGE_MAGIC, IMAGE_HEADER_VERSION, 0x00010002u, 1000u, 0xAABBCCDDu, 0u, 0u};
    h.header_crc32 = header_compute_crc(&h);
    return h;
}

static void test_serialize_layout(void)
{
    const image_header_t h = good();
    uint8_t b[IMAGE_HEADER_BYTES];
    CHECK_EQ(header_serialize(&h, b), BL_OK);
    CHECK(memcmp(b, "SBOT", 4) == 0); /* magic is little endian "SBOT" */
    CHECK_EQ(b[4], 1u);               /* header_version */
    CHECK_EQ(b[8], 0x02u);            /* fw_version LSB */
    CHECK_EQ(b[12], 0xE8u);           /* 1000 = 0x3E8 */
    CHECK_EQ(b[13], 0x03u);
    image_header_t back;
    CHECK_EQ(header_parse(b, sizeof(b), &back), BL_OK);
    CHECK(memcmp(&back, &h, sizeof(h)) == 0);
}

static void test_header_crc_covers_first_24_bytes(void)
{
    image_header_t h = good();
    uint8_t b[IMAGE_HEADER_BYTES];
    header_serialize(&h, b);
    CHECK_EQ(h.header_crc32, crc32_compute(b, 24u));
}

static void test_valid(void)
{
    const image_header_t h = good();
    CHECK_EQ(header_validate(&h, BL_APP_PAYLOAD_MAX_BYTES), BL_OK);
}

static void test_rejects(void)
{
    image_header_t h;

    h = good();
    h.magic ^= 1u;
    h.header_crc32 = header_compute_crc(&h); /* CRC fine, magic wrong */
    CHECK_EQ(header_validate(&h, BL_APP_PAYLOAD_MAX_BYTES), BL_ERR_BAD_HEADER);

    h = good();
    h.header_crc32 ^= 1u;
    CHECK_EQ(header_validate(&h, BL_APP_PAYLOAD_MAX_BYTES), BL_ERR_BAD_HEADER);

    h = good();
    h.image_size ^= 0x100u; /* field changed after CRC was computed */
    CHECK_EQ(header_validate(&h, BL_APP_PAYLOAD_MAX_BYTES), BL_ERR_BAD_HEADER);

    h = good();
    h.image_size = BL_APP_PAYLOAD_MAX_BYTES + 1u; /* oversized, but with a correct CRC */
    h.header_crc32 = header_compute_crc(&h);
    CHECK_EQ(header_validate(&h, BL_APP_PAYLOAD_MAX_BYTES), BL_ERR_BAD_HEADER);

    h = good();
    h.image_size = 0u;
    h.header_crc32 = header_compute_crc(&h);
    CHECK_EQ(header_validate(&h, BL_APP_PAYLOAD_MAX_BYTES), BL_ERR_BAD_HEADER);

    h = good();
    h.flags = 1u;
    h.header_crc32 = header_compute_crc(&h);
    CHECK_EQ(header_validate(&h, BL_APP_PAYLOAD_MAX_BYTES), BL_ERR_BAD_HEADER);

    h = good();
    h.header_version = 2u;
    h.header_crc32 = header_compute_crc(&h);
    CHECK_EQ(header_validate(&h, BL_APP_PAYLOAD_MAX_BYTES), BL_ERR_BAD_HEADER);
}

static void test_max_size_accepted(void)
{
    image_header_t h = good();
    h.image_size = BL_APP_PAYLOAD_MAX_BYTES;
    h.header_crc32 = header_compute_crc(&h);
    CHECK_EQ(header_validate(&h, BL_APP_PAYLOAD_MAX_BYTES), BL_OK);
}

static void test_erased_flash_is_invalid(void)
{
    uint8_t ff[IMAGE_HEADER_BYTES];
    memset(ff, 0xFF, sizeof(ff));
    image_header_t h;
    CHECK_EQ(header_parse(ff, sizeof(ff), &h), BL_OK);
    CHECK_EQ(header_validate(&h, BL_APP_PAYLOAD_MAX_BYTES), BL_ERR_BAD_HEADER);
}

static void test_parse_errors(void)
{
    uint8_t b[IMAGE_HEADER_BYTES] = {0};
    image_header_t h;
    CHECK_EQ(header_parse(b, IMAGE_HEADER_BYTES - 1u, &h), BL_ERR_BAD_LEN);
    CHECK_EQ(header_parse(NULL, IMAGE_HEADER_BYTES, &h), BL_ERR_INVALID_ARG);
}

int main(void)
{
    printf("test_header\n");
    RUN(test_serialize_layout);
    RUN(test_header_crc_covers_first_24_bytes);
    RUN(test_valid);
    RUN(test_rejects);
    RUN(test_max_size_accepted);
    RUN(test_erased_flash_is_invalid);
    RUN(test_parse_errors);
    TEST_SUMMARY("test_header");
}
