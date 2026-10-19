#include "updater.h"

#include <string.h>

#include "bytes.h"
#include "header.h"

#define HELLO_OFFSET_PROTO 0u
#define HELLO_OFFSET_STATE 1u
#define HELLO_OFFSET_MAX_DATA 2u
#define HELLO_OFFSET_BL_VERSION 4u
#define HELLO_OFFSET_SLOT_BYTES 8u

static void reply_ack(updater_reply_t *r, uint8_t seq)
{
    r->cmd = PROTO_CMD_ACK;
    r->seq = seq;
    r->len = 0u;
}

static void reply_nack(updater_reply_t *r, uint8_t seq, bl_status_t err)
{
    r->cmd = PROTO_CMD_NACK;
    r->seq = seq;
    r->payload[0] = (uint8_t)err;
    r->len = 1u;
}

static uint32_t round_up(uint32_t v, uint32_t unit)
{
    return ((v + unit - 1u) / unit) * unit;
}

/** Program the buffered bytes, padded with 0xFF (the erased value) to a full flash word. */
static bl_status_t flush_word(updater_t *u)
{
    if (u->word_fill == 0u) {
        return BL_OK;
    }
    memset(&u->word_buf[u->word_fill], 0xFF, BL_FLASH_WORD_BYTES - u->word_fill);
    const bl_status_t st = u->ops.write(u->ops.ctx, BL_APP_PAYLOAD_ADDR + u->flushed_bytes,
                                        u->word_buf, BL_FLASH_WORD_BYTES);
    if (st != BL_OK) {
        return BL_ERR_FLASH_WRITE;
    }
    u->flushed_bytes += u->word_fill;
    u->word_fill = 0u;
    return BL_OK;
}

static bl_status_t handle_hello(const updater_t *u, updater_reply_t *r)
{
    r->payload[HELLO_OFFSET_PROTO] = (uint8_t)PROTO_VERSION;
    r->payload[HELLO_OFFSET_STATE] = (uint8_t)u->state;
    le16_write(&r->payload[HELLO_OFFSET_MAX_DATA], (uint16_t)PROTO_MAX_DATA_BYTES);
    le32_write(&r->payload[HELLO_OFFSET_BL_VERSION], BL_VERSION);
    le32_write(&r->payload[HELLO_OFFSET_SLOT_BYTES], BL_APP_PAYLOAD_MAX_BYTES);
    r->len = PROTO_HELLO_REPLY_BYTES;
    return BL_OK;
}

static bl_status_t handle_start(updater_t *u, const proto_packet_t *req)
{
    image_header_t hdr;
    if (req->len != IMAGE_HEADER_BYTES) {
        return BL_ERR_BAD_LEN;
    }
    if ((header_parse(req->payload, req->len, &hdr) != BL_OK) ||
        (header_validate(&hdr, BL_APP_PAYLOAD_MAX_BYTES) != BL_OK)) {
        return BL_ERR_BAD_HEADER;
    }
    /* Only now is it safe to destroy the old image. Erase only the sectors that the
     * header area plus this image will cover. */
    u->state = UPD_IDLE;
    const uint32_t erase_len =
        round_up(BL_APP_HEADER_AREA_BYTES + hdr.image_size, BL_FLASH_SECTOR_BYTES);
    if (u->ops.erase(u->ops.ctx, BL_APP_SLOT_BASE_ADDR, erase_len) != BL_OK) {
        return BL_ERR_FLASH_ERASE;
    }
    u->header = hdr;
    u->received_bytes = 0u;
    u->flushed_bytes = 0u;
    u->word_fill = 0u;
    u->state = UPD_RECEIVING;
    return BL_OK;
}

static bl_status_t handle_data(updater_t *u, const proto_packet_t *req)
{
    if (u->state != UPD_RECEIVING) {
        return BL_ERR_NOT_READY;
    }
    if ((req->len <= PROTO_DATA_OFFSET_BYTES) || (req->len > PROTO_MAX_PAYLOAD_BYTES)) {
        return BL_ERR_BAD_LEN;
    }
    const uint32_t offset = le32_read(req->payload);
    const uint32_t n = req->len - PROTO_DATA_OFFSET_BYTES;
    const uint8_t *data = &req->payload[PROTO_DATA_OFFSET_BYTES];
    if ((offset != u->received_bytes) || (n > (u->header.image_size - offset))) {
        return BL_ERR_BAD_OFFSET;
    }
    for (uint32_t i = 0u; i < n; i++) {
        u->word_buf[u->word_fill] = data[i];
        u->word_fill++;
        u->received_bytes++;
        if (u->word_fill == BL_FLASH_WORD_BYTES) {
            const bl_status_t st = flush_word(u);
            if (st != BL_OK) {
                u->state = UPD_IDLE; /* half-programmed slot: the host must START again */
                return st;
            }
        }
    }
    if (u->received_bytes == u->header.image_size) {
        const bl_status_t st = flush_word(u); /* final partial word */
        if (st != BL_OK) {
            u->state = UPD_IDLE;
            return st;
        }
    }
    return BL_OK;
}

static bl_status_t handle_verify(updater_t *u)
{
    if ((u->state != UPD_RECEIVING) || (u->received_bytes != u->header.image_size)) {
        return BL_ERR_NOT_READY;
    }
    uint32_t crc = 0u;
    if (u->ops.crc32_flash(u->ops.ctx, BL_APP_PAYLOAD_ADDR, u->header.image_size, &crc) != BL_OK) {
        u->state = UPD_IDLE;
        return BL_ERR_VERIFY_FAIL;
    }
    if (crc != u->header.image_crc32) {
        u->state = UPD_IDLE;
        return BL_ERR_VERIFY_FAIL;
    }
    /* The header goes to flash LAST: until this point the slot cannot look valid. */
    uint8_t word[BL_FLASH_WORD_BYTES];
    memset(word, 0xFF, sizeof(word));
    (void)header_serialize(&u->header, word);
    if (u->ops.write(u->ops.ctx, BL_APP_SLOT_BASE_ADDR, word, BL_FLASH_WORD_BYTES) != BL_OK) {
        u->state = UPD_IDLE;
        return BL_ERR_FLASH_WRITE;
    }
    u->state = UPD_VERIFIED;
    return BL_OK;
}

void updater_init(updater_t *u, const flash_ops_t *ops)
{
    memset(u, 0, sizeof(*u));
    u->ops = *ops;
    u->state = UPD_IDLE;
}

bl_status_t updater_process(updater_t *u, const proto_packet_t *req, updater_reply_t *reply)
{
    bl_status_t st;

    /* Duplicate of the packet we already executed (our ACK was lost): ACK again, do nothing. */
    if (u->have_last && (req->cmd != PROTO_CMD_HELLO) && (req->seq == u->last_seq) &&
        (req->cmd == u->last_cmd)) {
        reply_ack(reply, req->seq);
        return BL_OK;
    }
    /* HELLO may arrive with any seq and resynchronises the numbering. */
    if (u->have_last && (req->cmd != PROTO_CMD_HELLO) &&
        (req->seq != (uint8_t)(u->last_seq + 1u))) {
        reply_nack(reply, req->seq, BL_ERR_BAD_SEQ);
        return BL_ERR_BAD_SEQ;
    }

    reply_ack(reply, req->seq);
    switch (req->cmd) {
    case PROTO_CMD_HELLO:
        st = handle_hello(u, reply);
        break;
    case PROTO_CMD_START:
        st = handle_start(u, req);
        break;
    case PROTO_CMD_DATA:
        st = handle_data(u, req);
        break;
    case PROTO_CMD_VERIFY:
        st = handle_verify(u);
        break;
    case PROTO_CMD_BOOT:
        st = (u->state == UPD_VERIFIED) ? BL_OK : BL_ERR_NOT_READY;
        u->boot_requested = (st == BL_OK);
        break;
    default:
        st = BL_ERR_BAD_CMD;
        break;
    }

    if (st != BL_OK) {
        reply_nack(reply, req->seq, st);
        return st;
    }
    u->have_last = true;
    u->last_seq = req->seq;
    u->last_cmd = req->cmd;
    return BL_OK;
}

updater_state_t updater_state(const updater_t *u)
{
    return u->state;
}

bool updater_boot_requested(const updater_t *u)
{
    return u->boot_requested;
}
