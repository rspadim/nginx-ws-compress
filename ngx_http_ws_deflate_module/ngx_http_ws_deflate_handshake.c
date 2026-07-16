#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_ws_deflate_handshake.h"
#include "ngx_http_ws_deflate_tunnel.h"


ngx_int_t
ngx_http_ws_deflate_request_handler(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_loc_conf_t  *conf;
    ngx_table_elt_t                 *ext;
    ngx_list_part_t                 *part;
    ngx_uint_t                       i;
    ngx_table_elt_t                 *h;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_ws_deflate_module);
    if (conf == NULL || (!conf->enabled && !conf->auto_detect)) {
        return NGX_DECLINED;
    }

    if (r->headers_in.upgrade == NULL
        || r->headers_in.upgrade->value.len != 9
        || ngx_strncasecmp(r->headers_in.upgrade->value.data,
                           (u_char *) "websocket", 9) != 0)
    {
        return NGX_DECLINED;
    }

    ext = NULL;
    part = &r->headers_in.headers.part;
    h = part->elts;

    for (i = 0; ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) break;
            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (h[i].key.len == 22
            && ngx_strncasecmp(h[i].key.data,
                               (u_char *) "sec-websocket-extensions", 22) == 0)
        {
            ext = &h[i];
            break;
        }
    }

    if (ext != NULL) {
        ext->hash = 0;
    }

    return NGX_DECLINED;
}


ngx_int_t
ngx_http_ws_deflate_handshake_handler(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_loc_conf_t  *conf;
    ngx_http_ws_deflate_ctx_t       *ctx;
    ngx_table_elt_t                 *h;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_ws_deflate_module);
    if (conf == NULL) {
        return NGX_DECLINED;
    }

    if (!conf->enabled && !conf->auto_detect) {
        return NGX_DECLINED;
    }

    if (r->headers_out.status != NGX_HTTP_SWITCHING_PROTOCOLS) {
        return NGX_DECLINED;
    }

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    h->hash = 1;
    ngx_str_set(&h->key, "Sec-WebSocket-Extensions");
    ngx_str_set(&h->value, "permessage-deflate");

    ctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_ws_deflate_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_ws_deflate_module);
    }

    ctx->initialized = 1;

    return NGX_OK;
}
