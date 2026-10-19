#include <string.h>

#include "cobs.h"
#include "protocol.h"
#include "test_util.h"

static void expect_vector(const uint8_t *dec, size_t dec_len, const uint8_t *enc, size_t enc_len)
{
    uint8_t out[600];
    size_t n = 0;
    CHECK_EQ(cobs_encode(dec, dec_len, out, sizeof(out), &n), BL_OK);
    CHECK_EQ(n, enc_len);
    CHECK(memcmp(out, enc, enc_len) == 0);
    CHECK_EQ(cobs_decode(enc, enc_len, out, sizeof(out), &n), BL_OK);
    CHECK_EQ(n, dec_len);
    CHECK(dec_len == 0u || memcmp(out, dec, dec_len) == 0);
}

static void test_reference_vectors(void)
{
    /* Vectors from the COBS paper / Wikipedia. */
    expect_vector((const uint8_t[]){0x00}, 1, (const uint8_t[]){0x01, 0x01}, 2);
    expect_vector((const uint8_t[]){0x00, 0x00}, 2, (const uint8_t[]){0x01, 0x01, 0x01}, 3);
    expect_vector((const uint8_t[]){0x00, 0x11, 0x00}, 3, (const uint8_t[]){0x01, 0x02, 0x11, 0x01},
                  4);
    expect_vector((const uint8_t[]){0x11, 0x22, 0x00, 0x33}, 4,
                  (const uint8_t[]){0x03, 0x11, 0x22, 0x02, 0x33}, 5);
    expect_vector((const uint8_t[]){0x11, 0x22, 0x33, 0x44}, 4,
                  (const uint8_t[]){0x05, 0x11, 0x22, 0x33, 0x44}, 5);
    expect_vector((const uint8_t[]){0x11, 0x00, 0x00, 0x00}, 4,
                  (const uint8_t[]){0x02, 0x11, 0x01, 0x01, 0x01}, 5);
    expect_vector(NULL, 0, (const uint8_t[]){0x01}, 1);
}

static void test_long_runs(void)
{
    uint8_t dec[256];
    uint8_t enc[260];
    /* 254 non zero bytes: single 0xFF block, no trailing code byte. */
    for (unsigned i = 0; i < 254; i++) {
        dec[i] = (uint8_t)(i + 1u);
        enc[i + 1u] = (uint8_t)(i + 1u);
    }
    enc[0] = 0xFF;
    expect_vector(dec, 254, enc, 255);
    /* 255 bytes 0x01..0xFF: 0xFF block then a 2 byte block. */
    dec[254] = 0xFF;
    enc[255] = 0x02;
    enc[256] = 0xFF;
    expect_vector(dec, 255, enc, 257);
    /* 0x00 followed by 254 non zero bytes. */
    uint8_t dec2[255];
    uint8_t enc2[257];
    dec2[0] = 0;
    enc2[0] = 0x01;
    enc2[1] = 0xFF;
    for (unsigned i = 0; i < 254; i++) {
        dec2[i + 1u] = (uint8_t)(i + 1u);
        enc2[i + 2u] = (uint8_t)(i + 1u);
    }
    expect_vector(dec2, 255, enc2, 256);
}

static void test_roundtrip_exhaustive_small(void)
{
    uint8_t in[8];
    uint8_t enc[32];
    uint8_t dec[32];
    /* every 4 byte pattern drawn from {0, 1, 0xFF} */
    for (unsigned v = 0; v < 81u; v++) {
        unsigned x = v;
        const uint8_t sym[3] = {0x00, 0x01, 0xFF};
        for (unsigned i = 0; i < 4; i++) {
            in[i] = sym[x % 3u];
            x /= 3u;
        }
        size_t el = 0;
        size_t dl = 0;
        CHECK_EQ(cobs_encode(in, 4, enc, sizeof(enc), &el), BL_OK);
        for (size_t i = 0; i < el; i++) {
            CHECK(enc[i] != 0u);
        }
        CHECK_EQ(cobs_decode(enc, el, dec, sizeof(dec), &dl), BL_OK);
        CHECK_EQ(dl, 4u);
        CHECK(memcmp(in, dec, 4) == 0);
    }
}

static void test_max_encoded_bound(void)
{
    uint8_t in[PROTO_MAX_RAW_BYTES];
    uint8_t enc[COBS_MAX_ENCODED_BYTES(PROTO_MAX_RAW_BYTES)];
    size_t el = 0;
    memset(in, 0x55, sizeof(in)); /* worst case: no zeros */
    CHECK_EQ(cobs_encode(in, sizeof(in), enc, sizeof(enc), &el), BL_OK);
    CHECK(el <= sizeof(enc));
}

static void test_errors(void)
{
    uint8_t out[8];
    size_t n = 0;
    const uint8_t zero_inside[] = {0x03, 0x11, 0x00};
    CHECK_EQ(cobs_decode(zero_inside, 3, out, sizeof(out), &n), BL_ERR_COBS);
    const uint8_t truncated[] = {0x05, 0x11, 0x22};
    CHECK_EQ(cobs_decode(truncated, 3, out, sizeof(out), &n), BL_ERR_COBS);
    const uint8_t zero_code[] = {0x00};
    CHECK_EQ(cobs_decode(zero_code, 1, out, sizeof(out), &n), BL_ERR_COBS);
    const uint8_t big[] = {0x05, 1, 2, 3, 4};
    CHECK_EQ(cobs_decode(big, 5, out, 2, &n), BL_ERR_OVERFLOW);
    uint8_t small[2];
    const uint8_t in[] = {1, 2, 3, 4};
    CHECK_EQ(cobs_encode(in, 4, small, sizeof(small), &n), BL_ERR_OVERFLOW);
    CHECK_EQ(cobs_encode(NULL, 1, small, sizeof(small), &n), BL_ERR_INVALID_ARG);
}

int main(void)
{
    printf("test_cobs\n");
    RUN(test_reference_vectors);
    RUN(test_long_runs);
    RUN(test_roundtrip_exhaustive_small);
    RUN(test_max_encoded_bound);
    RUN(test_errors);
    TEST_SUMMARY("test_cobs");
}
