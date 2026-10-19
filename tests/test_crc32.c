#include <string.h>

#include "crc32.h"
#include "test_util.h"

static void test_check_value(void)
{
    CHECK_EQ(crc32_compute((const uint8_t *)"123456789", 9u), CRC32_CHECK_VALUE);
}

static void test_empty(void)
{
    CHECK_EQ(crc32_compute(NULL, 0u), 0u);
}

static void test_streaming_matches_oneshot(void)
{
    uint8_t buf[300];
    for (size_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (uint8_t)(i * 7u + 3u);
    }
    uint32_t s = CRC32_INIT;
    s = crc32_update(s, buf, 1u);
    s = crc32_update(s, &buf[1], 100u);
    s = crc32_update(s, &buf[101], sizeof(buf) - 101u);
    CHECK_EQ(s ^ CRC32_XOR_OUT, crc32_compute(buf, sizeof(buf)));
}

static void test_known_vectors(void)
{
    /* Values from Python zlib.crc32. */
    const uint8_t zeros4[4] = {0, 0, 0, 0};
    CHECK_EQ(crc32_compute(zeros4, 4u), 0x2144DF1Cu);
    const uint8_t hello[4] = {1, 0, 0, 0};
    CHECK_EQ(crc32_compute(hello, 4u), 0x99F8B879u);
}

int main(void)
{
    printf("test_crc32\n");
    RUN(test_check_value);
    RUN(test_empty);
    RUN(test_streaming_matches_oneshot);
    RUN(test_known_vectors);
    TEST_SUMMARY("test_crc32");
}
