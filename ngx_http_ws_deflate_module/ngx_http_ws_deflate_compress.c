#include <ngx_config.h>
#include <ngx_core.h>

#include <zlib.h>

#include "ngx_http_ws_deflate_compress.h"


ngx_int_t
ngx_ws_deflate_ctx_init(ngx_ws_deflate_ctx_t *ctx, ngx_int_t level, ngx_flag_t takeover)
{
    int  rc;

    ctx->context_takeover = takeover;
    ctx->compression_level = level;
    ctx->initialized = 1;

    ctx->deflate_stream.zalloc = Z_NULL;
    ctx->deflate_stream.zfree = Z_NULL;
    ctx->deflate_stream.opaque = Z_NULL;

    rc = deflateInit2(&ctx->deflate_stream, level, Z_DEFLATED, -15, 8,
                          Z_DEFAULT_STRATEGY);
    if (rc != Z_OK) {
        return NGX_ERROR;
    }

    ctx->inflate_stream.zalloc = Z_NULL;
    ctx->inflate_stream.zfree = Z_NULL;
    ctx->inflate_stream.opaque = Z_NULL;

    rc = inflateInit2(&ctx->inflate_stream, -15);
    if (rc != Z_OK) {
        deflateEnd(&ctx->deflate_stream);
        return NGX_ERROR;
    }

    return NGX_OK;
}


ngx_int_t
ngx_ws_deflate_compress(ngx_ws_deflate_ctx_t *ctx, u_char *in, size_t in_len,
    u_char *out, size_t *out_len)
{
    z_stream  *strm;
    int        rc;
    size_t     written;

    strm = &ctx->deflate_stream;
    strm->avail_in = in_len;
    strm->next_in = in;
    strm->avail_out = *out_len;
    strm->next_out = out;

    rc = deflate(strm, Z_SYNC_FLUSH);
    if (rc != Z_OK) {
        return NGX_ERROR;
    }

    written = *out_len - strm->avail_out;

    if (written >= 4
        && out[written - 4] == 0x00
        && out[written - 3] == 0x00
        && out[written - 2] == 0xFF
        && out[written - 1] == 0xFF)
    {
        written -= 4;
    }

    *out_len = written;

    if (!ctx->context_takeover) {
        deflateReset(strm);
    }

    return NGX_OK;
}


ngx_int_t
ngx_ws_deflate_decompress(ngx_ws_deflate_ctx_t *ctx, u_char *in, size_t in_len,
    u_char *out, size_t *out_len)
{
    z_stream  *strm;
    int        rc;
    u_char     tail[4] = { 0x00, 0x00, 0xFF, 0xFF };

    strm = &ctx->inflate_stream;
    strm->avail_in = in_len;
    strm->next_in = in;
    strm->avail_out = *out_len;
    strm->next_out = out;

    rc = inflate(strm, Z_NO_FLUSH);
    if (rc != Z_OK) {
        return NGX_ERROR;
    }

    strm->avail_in = 4;
    strm->next_in = tail;

    rc = inflate(strm, Z_SYNC_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) {
        return NGX_ERROR;
    }

    *out_len = *out_len - strm->avail_out;

    if (!ctx->context_takeover) {
        inflateReset(strm);
    }

    return NGX_OK;
}


void
ngx_ws_deflate_ctx_destroy(ngx_ws_deflate_ctx_t *ctx)
{
    if (ctx->initialized) {
        deflateEnd(&ctx->deflate_stream);
        inflateEnd(&ctx->inflate_stream);
        ctx->initialized = 0;
    }
}
