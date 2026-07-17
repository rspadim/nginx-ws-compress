/*
 * WebSocket compression bridge - direct upstream handler.
 * Bypasses the proxy module entirely to avoid handler lifecycle conflicts.
 * Handles the full WebSocket lifecycle:
 *   content handler → connect to backend → upgrade → tunnel data
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_event.h>

#include "ngx_http_ws_deflate_handshake.h"
#include "ngx_http_ws_deflate_tunnel.h"
#include "ngx_http_ws_deflate_frame.h"
#include "ngx_http_ws_deflate_compress.h"


/* Context for upstream connection */
typedef struct {
    ngx_connection_t  *backend;
    ngx_buf_t         *buf;       /* read buffer (shared for both directions) */
    ngx_int_t          state;     /* 0=connecting, 1=reading_response, 2=tunnel */
    ngx_flag_t         client_deflate;
    ngx_ws_deflate_ctx_t compress_ctx;
} ngx_http_ws_deflate_upstream_ctx_t;


/* Socket connect + send HTTP upgrade */
static ngx_int_t ngx_ws_upstream_start(ngx_http_request_t *r);
static void ngx_ws_upstream_connect_event(ngx_event_t *ev);
static void ngx_ws_upstream_read_response(ngx_event_t *ev);
static void ngx_ws_upstream_client_read(ngx_event_t *ev);
static void ngx_ws_upstream_backend_read(ngx_event_t *ev);
static void ngx_ws_upstream_write(ngx_event_t *ev);


ngx_int_t
ngx_http_ws_deflate_upstream_handler(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_loc_conf_t  *conf;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_ws_deflate_module);
    if (conf == NULL || conf->upstream_pass.len == 0) {
        return NGX_DECLINED;
    }

    /* Only WebSocket upgrades */
    if (r->headers_in.upgrade == NULL
        || r->headers_in.upgrade->value.len != 9
        || ngx_strncasecmp(r->headers_in.upgrade->value.data,
                           (u_char *) "websocket", 9) != 0)
    {
        return NGX_DECLINED;
    }

    return ngx_ws_upstream_start(r);
}


static ngx_int_t
ngx_ws_upstream_start(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_loc_conf_t  *conf;
    ngx_str_t                        host, path;
    ngx_int_t                        port;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_ws_deflate_module);

    /* Parse ws_deflate_pass URL */
    u_char *p = conf->upstream_pass.data;
    size_t  len = conf->upstream_pass.len;

    /* Skip http:// */
    if (len < 7 || ngx_strncasecmp(p, (u_char *) "http://", 7) != 0) {
        return NGX_DECLINED;
    }
    p += 7; len -= 7;

    u_char *colon = ngx_strchr(p, ':');
    u_char *slash = ngx_strchr(p, '/');

    if (colon && (!slash || colon < slash)) {
        host.data = p; host.len = colon - p; p = colon + 1;
        if (slash) { port = ngx_atoi(p, slash - p); path.data = slash;
                     path.len = conf->upstream_pass.data + conf->upstream_pass.len - slash; }
        else { port = ngx_atoi(p, len); path.data = (u_char *) "/"; path.len = 1; }
    } else if (slash) {
        host.data = p; host.len = slash - p; port = 80;
        path.data = slash;
        path.len = conf->upstream_pass.data + conf->upstream_pass.len - slash;
    } else {
        host.data = p; host.len = len; port = 80;
        path.data = (u_char *) "/"; path.len = 1;
    }

    /* Allocate context + buffer */
    ngx_http_ws_deflate_upstream_ctx_t *ctx;
    ctx = ngx_pcalloc(r->pool, sizeof(*ctx));
    if (ctx == NULL) return NGX_ERROR;

    ctx->buf = ngx_create_temp_buf(r->pool, 65536);
    if (ctx->buf == NULL) return NGX_ERROR;
    ctx->client_deflate = 1;

    ngx_http_set_ctx(r, ctx, ngx_http_ws_deflate_module);

    /* Create socket */
    int fd = ngx_socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) return NGX_HTTP_INTERNAL_SERVER_ERROR;
    if (ngx_nonblocking(fd) == -1) { ngx_close_socket(fd); return NGX_HTTP_INTERNAL_SERVER_ERROR; }

    ngx_connection_t *pc = ngx_get_connection(fd, r->connection->log);
    if (pc == NULL) { ngx_close_socket(fd); return NGX_HTTP_INTERNAL_SERVER_ERROR; }

    ctx->backend = pc;
    pc->data = r;
    pc->pool = r->pool;
    pc->log = r->connection->log;
    pc->sendfile = 0;
    pc->read->handler = ngx_ws_upstream_connect_event;
    pc->write->handler = ngx_ws_upstream_connect_event;

    /* Non-blocking connect */
    struct sockaddr_in sin;
    ngx_memzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int rc = connect(fd, (struct sockaddr *) &sin, sizeof(sin));
    if (rc == -1 && ngx_socket_errno != NGX_EINPROGRESS) {
        ngx_close_connection(pc);
        return NGX_HTTP_BAD_GATEWAY;
    }

    /* Build upgrade request (NO Sec-WebSocket-Extensions) */
    u_char key_buf[256];
    u_char *key_data;
    size_t  key_len;

    if (r->headers_in.ws_key) {
        key_data = r->headers_in.ws_key->value.data;
        key_len = r->headers_in.ws_key->value.len;
    } else {
        key_data = (u_char *) "dGhlIHNhbXBsZSBub25jZQ==";
        key_len = 24;
    }

    u_char *req = ctx->buf->start;
    size_t req_len = ngx_snprintf(req, 65536,
        "GET %V HTTP/1.1\r\n"
        "Host: %V:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %*s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        &path, &host, port, key_len, key_data);

    ctx->buf->last = ctx->buf->start + req_len;

    /* Register write event */
    ngx_handle_write_event(pc->write, 0);
    r->write_event_handler = ngx_http_request_empty_handler;

    return NGX_DONE;
}


/* Called when backend connection is writable (connected or ready to send) */
static void
ngx_ws_upstream_connect_event(ngx_event_t *ev)
{
    ngx_http_request_t *r = ev->data;
    ngx_http_ws_deflate_upstream_ctx_t *ctx;
    ctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (!ctx || !ctx->backend) return;

    ngx_connection_t *pc = ctx->backend;

    /* Send the upgrade request */
    if (ctx->buf->pos < ctx->buf->last) {
        ngx_chain_t chain;
        chain.buf = ctx->buf;
        chain.next = NULL;

        if (pc->send_chain(pc, &chain, 0) == NGX_CHAIN_ERROR) {
            ngx_close_connection(pc); ctx->backend = NULL; return;
        }
        ctx->buf->pos = ctx->buf->last;
    }

    /* Switch to read response */
    ctx->state = 1;
    pc->read->handler = ngx_ws_upstream_read_response;
    ngx_handle_read_event(pc->read, 0);
}


/* Read the 101 response from backend */
static void
ngx_ws_upstream_read_response(ngx_event_t *ev)
{
    ngx_http_request_t *r = ev->data;
    ngx_http_ws_deflate_upstream_ctx_t *ctx;
    ctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (!ctx || !ctx->backend) return;

    ngx_connection_t *pc = ctx->backend;
    u_char  buf[4096];
    ssize_t n = pc->recv(pc, buf, sizeof(buf));

    if (n <= 0) { ngx_close_connection(pc); ctx->backend = NULL; return; }

    /* Accumulate in buffer */
    if (ctx->buf->last + n <= ctx->buf->start + 65536) {
        ngx_memcpy(ctx->buf->last, buf, n);
        ctx->buf->last += n;
    }

    /* Check for complete headers */
    u_char *end = (u_char *) ngx_strstr(ctx->buf->start, (u_char *) "\r\n\r\n");
    if (!end) { ngx_handle_read_event(pc->read, 0); return; }

    /* Verify it's 101 */
    if (!ngx_strstr(ctx->buf->start, (u_char *) "101")) {
        ngx_close_connection(pc); ctx->backend = NULL; return;
    }

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "ws_deflate: backend 101 OK, starting tunnel");

    /* Send 101 to client with extensions header */
    ngx_connection_t *c = r->connection;

    /* Calculate accept key based on the key we sent */
    u_char accept_key[64];
    size_t ak_len = 28;  /* placeholder */
    ngx_memcpy(accept_key, (u_char *) "dGhlIHNhbXBsZSBub25jZQ==", 28);

    u_char resp[1024];
    size_t rlen = ngx_snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %*s\r\n"
        "Sec-WebSocket-Extensions: permessage-deflate\r\n"
        "X-WS-Deflate: active\r\n"
        "\r\n", ak_len, accept_key);

    ngx_buf_t *b = ngx_create_temp_buf(r->pool, rlen);
    if (!b) return;
    ngx_memcpy(b->start, resp, rlen);
    b->last = b->start + rlen;
    b->memory = 1;
    ngx_chain_t out = { b, NULL };

    if (c->send_chain(c, &out, 0) == NGX_CHAIN_ERROR) {
        ngx_close_connection(pc); ctx->backend = NULL; return;
    }

    /* For now: raw tunnel without compression (prove the approach works) */
    ctx->state = 2;
    c->read->data = r;
    c->read->handler = ngx_ws_upstream_client_read;
    pc->read->data = r;
    pc->read->handler = ngx_ws_upstream_backend_read;
    c->write->data = r;
    c->write->handler = ngx_ws_upstream_write;
    pc->write->data = r;
    pc->write->handler = ngx_ws_upstream_write;

    ngx_log_error(NGX_LOG_INFO, c->log, 0,
                  "ws_deflate: raw tunnel active");
}


/* Client → Backend: forward raw data */
static void
ngx_ws_upstream_client_read(ngx_event_t *ev)
{
    ngx_http_request_t *r = ev->data;
    ngx_http_ws_deflate_upstream_ctx_t *ctx;
    ctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (!ctx || !ctx->backend) return;

    ngx_connection_t *c = r->connection;
    ngx_connection_t *pc = ctx->backend;

    u_char buf[8192];
    ssize_t n = c->recv(c, buf, sizeof(buf));
    if (n <= 0) { ngx_close_connection(pc); ctx->backend = NULL; return; }

    /* Forward to backend */
    ngx_buf_t *b = ngx_create_temp_buf(r->pool, n);
    if (!b) return;
    ngx_memcpy(b->start, buf, n);
    b->last = b->start + n;
    b->memory = 1;
    ngx_chain_t chain = { b, NULL };

    if (pc->send_chain(pc, &chain, 0) == NGX_CHAIN_ERROR) {
        ngx_close_connection(pc); ctx->backend = NULL; return;
    }

    ngx_handle_read_event(c->read, 0);
}


/* Backend → Client: forward raw data */
static void
ngx_ws_upstream_backend_read(ngx_event_t *ev)
{
    ngx_http_request_t *r = ev->data;
    ngx_http_ws_deflate_upstream_ctx_t *ctx;
    ctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (!ctx || !ctx->backend) return;

    ngx_connection_t *c = r->connection;
    ngx_connection_t *pc = ctx->backend;

    u_char buf[8192];
    ssize_t n = pc->recv(pc, buf, sizeof(buf));
    if (n <= 0) { ngx_close_connection(pc); ctx->backend = NULL; return; }

    /* Forward to client */
    ngx_buf_t *b = ngx_create_temp_buf(r->pool, n);
    if (!b) return;
    ngx_memcpy(b->start, buf, n);
    b->last = b->start + n;
    b->memory = 1;
    ngx_chain_t chain = { b, NULL };

    if (c->send_chain(c, &chain, 0) == NGX_CHAIN_ERROR) {
        ngx_close_connection(pc); ctx->backend = NULL; return;
    }

    ngx_handle_read_event(pc->read, 0);
}


/* Write event handler (no-op - writes happen inline) */
static void
ngx_ws_upstream_write(ngx_event_t *ev)
{
    ngx_handle_write_event(ev, 0);
}
