#ifndef _NGX_HTTP_WS_DEFLATE_COMPRESS_H_INCLUDED_
#define _NGX_HTTP_WS_DEFLATE_COMPRESS_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>

#include <zlib.h>

typedef struct {
    z_stream    deflate_stream;
    z_stream    inflate_stream;
    ngx_flag_t    context_takeover;
    ngx_int_t     compression_level;
    ngx_flag_t    initialized;
} ngx_ws_deflate_ctx_t;

ngx_int_t ngx_ws_deflate_ctx_init(ngx_ws_deflate_ctx_t *ctx, ngx_int_t level, ngx_flag_t takeover);
ngx_int_t ngx_ws_deflate_compress(ngx_ws_deflate_ctx_t *ctx, u_char *in, size_t in_len, u_char *out, size_t *out_len);
ngx_int_t ngx_ws_deflate_decompress(ngx_ws_deflate_ctx_t *ctx, u_char *in, size_t in_len, u_char *out, size_t *out_len);
void ngx_ws_deflate_ctx_destroy(ngx_ws_deflate_ctx_t *ctx);

#endif /* _NGX_HTTP_WS_DEFLATE_COMPRESS_H_INCLUDED_ */
