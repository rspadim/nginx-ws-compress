#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_ws_deflate_handshake.h"
#include "ngx_http_ws_deflate_tunnel.h"


ngx_int_t
ngx_http_ws_deflate_request_handler(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_loc_conf_t        *conf;
    ngx_http_ws_deflate_main_conf_t       *mcf;
    ngx_http_ws_deflate_tunnel_ctx_t      *ctx;
    ngx_table_elt_t                       *ext, *h;
    ngx_list_part_t                       *part;
    ngx_uint_t                             i;

    if (ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module) != NULL) {
        return NGX_DECLINED;
    }

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

    /* Check ws_deflate_except pattern */
    mcf = ngx_http_get_module_main_conf(r, ngx_http_ws_deflate_module);
    if (mcf != NULL && mcf->except_pattern.len > 0) {
        u_char *pat = mcf->except_pattern.data;
        size_t  pat_len = mcf->except_pattern.len;
        ngx_uint_t is_regex = (pat_len > 1 && pat[0] == '~');

        if (is_regex) {
            /* Simple prefix match after ~ */
            u_char *prefix = pat + 1;
            size_t  prefix_len = pat_len - 1;
            while (prefix_len > 0 && *prefix == ' ') { prefix++; prefix_len--; }
            if (r->uri.len >= prefix_len
                && ngx_strncmp(r->uri.data, prefix, prefix_len) == 0)
            {
                return NGX_DECLINED;
            }
        } else {
            /* Prefix match */
            if (r->uri.len >= pat_len
                && ngx_strncmp(r->uri.data, pat, pat_len) == 0)
            {
                return NGX_DECLINED;
            }
        }
    }

    /* Find Sec-WebSocket-Extensions header */
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

    /* Allocate context and record whether client requested deflate */
    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_ws_deflate_tunnel_ctx_t));
    if (ctx == NULL) {
        return NGX_ERROR;
    }
    ngx_http_set_ctx(r, ctx, ngx_http_ws_deflate_module);

    if (ext != NULL) {
        if (ngx_strstr(ext->value.data, (u_char *) "permessage-deflate") != NULL) {
            ctx->client_deflate = 1;
        }
        /* Remove extensions header so backend doesn't see it */
        ext->hash = 0;
    }

    return NGX_DECLINED;
}


ngx_int_t
ngx_http_ws_deflate_handshake_handler(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_loc_conf_t       *conf;
    ngx_http_ws_deflate_tunnel_ctx_t     *ctx;
    ngx_table_elt_t                      *h;

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

    ctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);

    /* Count this connection regardless of compression negotiation */
    ngx_ws_deflate_total_connections++;

    /* Only add Sec-WebSocket-Extensions if client requested deflate */
    if (ctx != NULL && ctx->client_deflate) {
        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            return NGX_ERROR;
        }

        h->hash = 1;
        ngx_str_set(&h->key, "Sec-WebSocket-Extensions");
        ngx_str_set(&h->value,
            "permessage-deflate; client_max_window_bits=15; server_max_window_bits=15");

        /* Add diagnostic header so clients can confirm compression is active */
        h = ngx_list_push(&r->headers_out.headers);
        if (h == NULL) {
            return NGX_ERROR;
        }
        h->hash = 1;
        ngx_str_set(&h->key, "X-WS-Deflate");
        ngx_str_set(&h->value, "active");

        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                      "ws_deflate: negotiated permessage-deflate with client");
    }

    /* Install the tunnel to intercept WebSocket frames.
     * Only needed when compression is negotiated.
     * Without compression, nginx native proxy pass-through works fine. */
    if (ctx != NULL && ctx->client_deflate) {
        if (ngx_http_ws_deflate_tunnel_install(r) != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "ws_deflate: failed to install tunnel");
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}
