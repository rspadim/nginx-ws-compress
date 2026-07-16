#ifndef _NGX_HTTP_WS_DEFLATE_TUNNEL_H_INCLUDED_
#define _NGX_HTTP_WS_DEFLATE_TUNNEL_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_ws_deflate_compress.h"
#include "ngx_http_ws_deflate_handshake.h"


#define NGX_WS_DEFLATE_BUF_SIZE 65536  /* 64KB reusable buffers */


typedef struct {
    ngx_ws_deflate_ctx_t         compress_ctx;       /* compression context */
    ngx_buf_t                   *client_buf;          /* buffer: client→upstream */
    ngx_buf_t                   *upstream_buf;        /* buffer: upstream→client */
    ngx_connection_t            *client_connection;
    ngx_connection_t            *upstream_connection;
    ngx_http_ws_deflate_loc_conf_t *conf;             /* module config snapshot */
    ngx_pool_t                  *pool;                /* request pool */
    ngx_flag_t                   initialized;         /* context is ready */
    ngx_flag_t                   client_deflate;      /* client negotiated deflate */

    /* Reusable scratch buffers for per-frame compress/decompress.
     * Allocated once at tunnel creation, reused for every frame.
     * Eliminates per-frame pool allocations for long-lived connections. */
    u_char                      *tmp_compress;        /* [NGX_WS_DEFLATE_BUF_SIZE] */
    u_char                      *tmp_decompress;      /* [NGX_WS_DEFLATE_BUF_SIZE] */
} ngx_http_ws_deflate_tunnel_ctx_t;


ngx_int_t ngx_http_ws_deflate_tunnel_install(ngx_http_request_t *r);
void ngx_http_ws_deflate_tunnel_close(ngx_http_request_t *r);


#endif /* _NGX_HTTP_WS_DEFLATE_TUNNEL_H_INCLUDED_ */
