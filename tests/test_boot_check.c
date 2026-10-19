#include <string.h>

#include "boot_check.h"
#include "crc32.h"
#include "fake_flash.h"
#include "header.h"
#include "test_util.h"

static fake_flash_t g_flash;

/* Write a bootable image of `size` bytes into the fake slot. vectors: sp, reset. */
static void install(uint32_t size, uint32_t sp, uint32_t reset)
{
    fake_flash_reset(&g_flash);
    uint8_t *payload = &g_flash.mem[BL_APP_HEADER_AREA_BYTES];
    for (uint32_t i = 0; i < size; i++) {
        payload[i] = (uint8_t)(i * 13u + 5u);
    }
    payload[0] = (uint8_t)sp;
    payload[1] = (uint8_t)(sp >> 8);
    payload[2] = (uint8_t)(sp >> 16);
    payload[3] = (uint8_t)(sp >> 24);
    payload[4] = (uint8_t)reset;
    payload[5] = (uint8_t)(reset >> 8);
    payload[6] = (uint8_t)(reset >> 16);
    payload[7] = (uint8_t)(reset >> 24);
    image_header_t h = {
        IMAGE_MAGIC, IMAGE_HEADER_VERSION, 1u, size, crc32_compute(payload, size), 0u, 0u};
    h.header_crc32 = header_compute_crc(&h);
    header_serialize(&h, g_flash.mem);
}

static bl_status_t check(void)
{
    const flash_ops_t ops = fake_flash_ops(&g_flash);
    return boot_check_slot(g_flash.mem, &ops, NULL);
}

static void test_valid_slot(void)
{
    install(1000, 0x24080000u, BL_APP_PAYLOAD_ADDR + 0x200u + 1u);
    CHECK_EQ(check(), BL_OK);
    install(1000, 0x20020000u, BL_APP_PAYLOAD_ADDR + 1u); /* stack in DTCM */
    CHECK_EQ(check(), BL_OK);
}

static void test_erased_slot(void)
{
    fake_flash_reset(&g_flash);
    CHECK_EQ(check(), BL_ERR_BAD_HEADER);
}

static void test_flipped_payload_byte(void)
{
    install(1000, 0x24080000u, BL_APP_PAYLOAD_ADDR + 0x201u);
    g_flash.mem[BL_APP_HEADER_AREA_BYTES + 500u] ^= 0x01u;
    CHECK_EQ(check(), BL_ERR_VERIFY_FAIL);
}

static void test_flipped_header_byte(void)
{
    install(1000, 0x24080000u, BL_APP_PAYLOAD_ADDR + 0x201u);
    g_flash.mem[13] ^= 0x01u;
    CHECK_EQ(check(), BL_ERR_BAD_HEADER);
}

static void test_bad_vectors(void)
{
    const uint32_t good_reset = BL_APP_PAYLOAD_ADDR + 0x101u;
    CHECK_EQ(boot_check_vectors(0x24080000u, good_reset, 1000u), BL_OK);
    CHECK_EQ(boot_check_vectors(0x00000000u, good_reset, 1000u), BL_ERR_BAD_VECTORS);
    CHECK_EQ(boot_check_vectors(0xFFFFFFFFu, good_reset, 1000u), BL_ERR_BAD_VECTORS);
    CHECK_EQ(boot_check_vectors(0x24000000u, good_reset, 1000u), BL_ERR_BAD_VECTORS); /* base */
    CHECK_EQ(boot_check_vectors(0x24080004u, good_reset, 1000u), BL_ERR_BAD_VECTORS); /* past end */
    CHECK_EQ(boot_check_vectors(0x2407FFFCu, good_reset, 1000u),
             BL_ERR_BAD_VECTORS); /* 4 aligned */
    CHECK_EQ(boot_check_vectors(0x24080000u, good_reset & ~1u, 1000u),
             BL_ERR_BAD_VECTORS);                                                      /* ARM */
    CHECK_EQ(boot_check_vectors(0x24080000u, 0x08000101u, 1000u), BL_ERR_BAD_VECTORS); /* in BL */
    CHECK_EQ(boot_check_vectors(0x24080000u, BL_APP_PAYLOAD_ADDR + 1000u + 1u, 1000u),
             BL_ERR_BAD_VECTORS); /* beyond the image */
    CHECK_EQ(boot_check_vectors(0x24080000u, 0xFFFFFFFFu, 1000u), BL_ERR_BAD_VECTORS);
}

static void test_slot_with_bad_vectors_not_bootable(void)
{
    install(1000, 0xFFFFFFFFu, 0xFFFFFFFFu); /* valid CRCs, garbage vector table */
    CHECK_EQ(check(), BL_ERR_BAD_VECTORS);
}

static void test_null_args(void)
{
    const flash_ops_t ops = fake_flash_ops(&g_flash);
    CHECK_EQ(boot_check_slot(NULL, &ops, NULL), BL_ERR_INVALID_ARG);
    CHECK_EQ(boot_check_slot(g_flash.mem, NULL, NULL), BL_ERR_INVALID_ARG);
}

int main(void)
{
    printf("test_boot_check\n");
    RUN(test_valid_slot);
    RUN(test_erased_slot);
    RUN(test_flipped_payload_byte);
    RUN(test_flipped_header_byte);
    RUN(test_bad_vectors);
    RUN(test_slot_with_bad_vectors_not_bootable);
    RUN(test_null_args);
    TEST_SUMMARY("test_boot_check");
}
