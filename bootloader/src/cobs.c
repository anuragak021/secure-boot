#include "cobs.h"

#include <stdbool.h>

#define COBS_MAX_RUN 0xFFu /* code 0xFF = 254 data bytes follow and no implicit zero */

bl_status_t cobs_encode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max,
                        size_t *out_len)
{
    if (((in == NULL) && (in_len != 0u)) || (out == NULL) || (out_len == NULL)) {
        return BL_ERR_INVALID_ARG;
    }
    if (out_max == 0u) {
        return BL_ERR_OVERFLOW;
    }

    size_t code_pos = 0u; /* where the current block's code byte will be stored */
    size_t o = 1u;
    uint8_t code = 1u;
    bool closed_full_run = false; /* last byte completed a 254 byte run: no trailing block needed */

    for (size_t i = 0; i < in_len; i++) {
        closed_full_run = false;
        if (in[i] == 0u) {
            out[code_pos] = code;
            code_pos = o;
            o++;
            code = 1u;
        } else {
            if (o >= out_max) {
                return BL_ERR_OVERFLOW;
            }
            out[o] = in[i];
            o++;
            code++;
            if (code == COBS_MAX_RUN) {
                out[code_pos] = code;
                code_pos = o;
                o++;
                code = 1u;
                closed_full_run = true;
            }
        }
        if (o > out_max) { /* the code byte reserved above must fit too */
            return BL_ERR_OVERFLOW;
        }
    }
    if (closed_full_run) {
        o--; /* drop the unused placeholder: this is the minimal (reference) encoding */
    } else {
        out[code_pos] = code;
    }
    *out_len = o;
    return BL_OK;
}

bl_status_t cobs_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max,
                        size_t *out_len)
{
    if ((in == NULL) || (out == NULL) || (out_len == NULL)) {
        return BL_ERR_INVALID_ARG;
    }
    size_t i = 0u;
    size_t o = 0u;
    while (i < in_len) {
        const uint8_t code = in[i];
        i++;
        if (code == 0u) {
            return BL_ERR_COBS;
        }
        if ((size_t)(code - 1u) > (in_len - i)) {
            return BL_ERR_COBS; /* block claims more bytes than remain */
        }
        if ((size_t)(code - 1u) > (out_max - o)) {
            return BL_ERR_OVERFLOW;
        }
        for (uint8_t j = 1u; j < code; j++) {
            if (in[i] == 0u) {
                return BL_ERR_COBS;
            }
            out[o] = in[i];
            o++;
            i++;
        }
        if ((code < COBS_MAX_RUN) && (i < in_len)) {
            if (o >= out_max) {
                return BL_ERR_OVERFLOW;
            }
            out[o] = 0u; /* the zero that ended this block */
            o++;
        }
    }
    *out_len = o;
    return BL_OK;
}
