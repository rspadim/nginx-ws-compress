#ifndef _NGX_HTTP_WS_DEFLATE_FRAME_H_INCLUDED_
#define _NGX_HTTP_WS_DEFLATE_FRAME_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>

#define NGX_WS_OPCODE_CONTINUATION  0x0
#define NGX_WS_OPCODE_TEXT          0x1
#define NGX_WS_OPCODE_BINARY        0x2
#define NGX_WS_OPCODE_CLOSE         0x8
#define NGX_WS_OPCODE_PING          0x9
#define NGX_WS_OPCODE_PONG          0xA

#define NGX_WS_FLAG_FIN   0x80
#define NGX_WS_FLAG_RSV1  0x40
#define NGX_WS_FLAG_RSV2  0x20
#define NGX_WS_FLAG_RSV3  0x10
#define NGX_WS_FLAG_MASK  0x80

#define NGX_WS_MAX_PAYLOAD (16 * 1024 * 1024)

typedef struct {
    ngx_uint_t   fin;
    ngx_uint_t   rsv1, rsv2, rsv3;
    ngx_uint_t   opcode;
    ngx_uint_t   masked;
    uint32_t     masking_key;
    u_char      *payload;
    size_t       payload_len;
    size_t       header_len;
} ngx_ws_frame_t;

ngx_int_t ngx_ws_frame_parse(u_char *data, size_t len, ngx_ws_frame_t *frame);
ngx_int_t ngx_ws_frame_serialize(ngx_ws_frame_t *frame, u_char *buf, size_t *len);
void ngx_ws_frame_apply_mask(u_char *payload, size_t len, uint32_t masking_key);
void ngx_ws_frame_generate_mask(uint32_t *key);

#endif /* _NGX_HTTP_WS_DEFLATE_FRAME_H_INCLUDED_ */
