#include <ngx_config.h>
#include <ngx_core.h>

#include "ngx_http_ws_deflate_frame.h"


ngx_int_t
ngx_ws_frame_parse(u_char *data, size_t len, ngx_ws_frame_t *frame)
{
    u_char   b;
    size_t   need, plen;
    uint64_t extended;

    if (len < 2) {
        return NGX_AGAIN;
    }

    b = data[0];
    frame->fin    = (b & NGX_WS_FLAG_FIN)  ? 1 : 0;
    frame->rsv1   = (b & NGX_WS_FLAG_RSV1) ? 1 : 0;
    frame->rsv2   = (b & NGX_WS_FLAG_RSV2) ? 1 : 0;
    frame->rsv3   = (b & NGX_WS_FLAG_RSV3) ? 1 : 0;
    frame->opcode = b & 0x0F;

    b = data[1];
    frame->masked = (b & NGX_WS_FLAG_MASK) ? 1 : 0;
    plen = b & 0x7F;

    if (plen == 126) {
        if (len < 4) {
            return NGX_AGAIN;
        }
        extended = (data[2] << 8) | data[3];
        need = 4;

    } else if (plen == 127) {
        if (len < 10) {
            return NGX_AGAIN;
        }
        extended = 0;
        extended |= (uint64_t) data[2] << 56;
        extended |= (uint64_t) data[3] << 48;
        extended |= (uint64_t) data[4] << 40;
        extended |= (uint64_t) data[5] << 32;
        extended |= (uint64_t) data[6] << 24;
        extended |= (uint64_t) data[7] << 16;
        extended |= (uint64_t) data[8] << 8;
        extended |= (uint64_t) data[9];
        need = 10;

    } else {
        extended = plen;
        need = 2;
    }

    if (extended > NGX_WS_MAX_PAYLOAD) {
        return NGX_ERROR;
    }

    frame->payload_len = (size_t) extended;

    if (frame->masked) {
        if (len < need + 4) {
            return NGX_AGAIN;
        }
        frame->masking_key = ((uint32_t) data[need] << 24)
                           | ((uint32_t) data[need + 1] << 16)
                           | ((uint32_t) data[need + 2] << 8)
                           | (uint32_t) data[need + 3];
        need += 4;
    }

    if (len < need + frame->payload_len) {
        return NGX_AGAIN;
    }

    frame->header_len = need;
    frame->payload = data + need;

    return NGX_OK;
}


ngx_int_t
ngx_ws_frame_serialize(ngx_ws_frame_t *frame, u_char *buf, size_t *len)
{
    u_char   *p;
    size_t    need;
    uint64_t  plen;

    p = buf;

    p[0] = 0;
    if (frame->fin)  p[0] |= NGX_WS_FLAG_FIN;
    if (frame->rsv1) p[0] |= NGX_WS_FLAG_RSV1;
    if (frame->rsv2) p[0] |= NGX_WS_FLAG_RSV2;
    if (frame->rsv3) p[0] |= NGX_WS_FLAG_RSV3;
    p[0] |= frame->opcode & 0x0F;

    p[1] = 0;
    if (frame->masked) p[1] |= NGX_WS_FLAG_MASK;

    plen = frame->payload_len;

    if (plen < 126) {
        p[1] |= (u_char) plen;
        need = 2;

    } else if (plen < 65536) {
        p[1] |= 126;
        p[2] = (u_char) (plen >> 8);
        p[3] = (u_char)  plen;
        need = 4;

    } else {
        p[1] |= 127;
        p[2] = (u_char) (plen >> 56);
        p[3] = (u_char) (plen >> 48);
        p[4] = (u_char) (plen >> 40);
        p[5] = (u_char) (plen >> 32);
        p[6] = (u_char) (plen >> 24);
        p[7] = (u_char) (plen >> 16);
        p[8] = (u_char) (plen >> 8);
        p[9] = (u_char)  plen;
        need = 10;
    }

    if (frame->masked) {
        p[need]     = (u_char) (frame->masking_key >> 24);
        p[need + 1] = (u_char) (frame->masking_key >> 16);
        p[need + 2] = (u_char) (frame->masking_key >> 8);
        p[need + 3] = (u_char)  frame->masking_key;
        need += 4;
    }

    if (frame->payload != NULL && frame->payload_len > 0) {
        ngx_memcpy(p + need, frame->payload, frame->payload_len);
    }

    *len = need + frame->payload_len;

    return NGX_OK;
}


void
ngx_ws_frame_apply_mask(u_char *payload, size_t len, uint32_t masking_key)
{
    u_char  key[4];
    size_t  i;

    key[0] = (u_char) (masking_key >> 24);
    key[1] = (u_char) (masking_key >> 16);
    key[2] = (u_char) (masking_key >> 8);
    key[3] = (u_char)  masking_key;

    for (i = 0; i < len; i++) {
        payload[i] ^= key[i % 4];
    }
}


void
ngx_ws_frame_generate_mask(uint32_t *key)
{
    /* Simple pseudo-random mask without nginx dependencies.
     * Key is based on address of key itself plus file-scoped counter,
     * which provides sufficient entropy for WebSocket masking. */
    static ngx_uint_t  mask_seed = 0;
    mask_seed = (mask_seed + 1) * 1103515245 + 12345;
    *key = (uint32_t) mask_seed ^ (uint32_t) ((ngx_int_t) key >> 2);
}
