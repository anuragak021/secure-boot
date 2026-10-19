#include "session.h"

#include "protocol.h"
#include "protocol_codec.h"

static bl_status_t send_reply(const updater_reply_t *r, session_send_fn send)
{
    uint8_t frame[PROTO_MAX_FRAME_BYTES];
    size_t n = 0u;
    const bl_status_t st =
        proto_encode(r->cmd, r->seq, r->payload, r->len, frame, sizeof(frame), &n);
    if (st != BL_OK) {
        return st;
    }
    return send(frame, n);
}

bl_status_t session_handle_frame(updater_t *upd, const uint8_t *frame, size_t len,
                                 session_send_fn send)
{
    uint8_t scratch[PROTO_MAX_RAW_BYTES];
    proto_packet_t pkt;
    updater_reply_t reply;

    const bl_status_t st = proto_decode(frame, len, scratch, sizeof(scratch), &pkt);
    if (st == BL_ERR_BAD_CRC) {
        reply.cmd = PROTO_CMD_NACK;
        reply.seq = pkt.seq;
        reply.payload[0] = (uint8_t)BL_ERR_BAD_CRC;
        reply.len = 1u;
        (void)send_reply(&reply, send);
        return st;
    }
    if (st != BL_OK) {
        return st;
    }
    const bl_status_t result = updater_process(upd, &pkt, &reply);
    const bl_status_t tx = send_reply(&reply, send);
    return (tx != BL_OK) ? tx : result;
}
