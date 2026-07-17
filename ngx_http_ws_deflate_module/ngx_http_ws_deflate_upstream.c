/*
 * WebSocket compression bridge - direct upstream handler.
 * Bypasses the proxy module entirely.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_event.h>

#include "ngx_http_ws_deflate_handshake.h"


/* Context */
typedef struct {
    ngx_connection_t  *backend;
    ngx_buf_t         *buf;       /* shared buffer for request + response */
    ngx_int_t          state;     /* 0=connecting, 1=reading_response, 2=tunnel */
} ngx_http_ws_deflate_upstream_ctx_t;


static void ngx_ws_upstream_send_request(ngx_event_t *ev);
static void ngx_ws_upstream_read_response(ngx_event_t *ev);
static void ngx_ws_upstream_tunnel_read(ngx_event_t *ev);
static void ngx_ws_upstream_tunnel_write(ngx_event_t *ev);


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

    /* Parse ws_deflate_pass URL */
    u_char *p = conf->upstream_pass.data;
    size_t  len = conf->upstream_pass.len;

    if (len < 7 || ngx_strncasecmp(p, (u_char *) "http://", 7) != 0) {
        return NGX_DECLINED;
    }
    p += 7; len -= 7;

    ngx_str_t host, path;
    ngx_int_t port;
    u_char *colon = (u_char *) ngx_strchr(p, ':');
    u_char *slash = (u_char *) ngx_strchr(p, '/');

    if (colon && (!slash || colon < slash)) {
        host.data = p; host.len = colon - p; p = colon + 1;
        if (slash) { port = ngx_atoi(p, slash - p);
                     path.data = slash;
                     path.len = (size_t)(conf->upstream_pass.data + conf->upstream_pass.len - slash); }
        else { port = ngx_atoi(p, len);
               path.data = (u_char *) "/"; path.len = 1; }
    } else if (slash) {
        host.data = p; host.len = slash - p; port = 80;
        path.data = slash;
        path.len = (size_t)(conf->upstream_pass.data + conf->upstream_pass.len - slash);
    } else {
        host.data = p; host.len = len; port = 80;
        path.data = (u_char *) "/"; path.len = 1;
    }

    /* Find the client's WebSocket key from headers */
    ngx_str_t  ws_key;
    ngx_memzero(&ws_key, sizeof(ws_key));
    ngx_list_part_t *part = &r->headers_in.headers.part;
    ngx_table_elt_t *h = part->elts;
    ngx_uint_t i;

    for (i = 0; ; i++) {
        if (i >= part->nelts) {
            if (part->next == NULL) break;
            part = part->next;
            h = part->elts;
            i = 0;
        }
        if (h[i].key.len == 17
            && ngx_strncasecmp(h[i].key.data, (u_char *) "sec-websocket-key", 17) == 0)
        {
            ws_key = h[i].value;
            break;
        }
    }

    /* Create socket */
    int fd = ngx_socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) return NGX_HTTP_INTERNAL_SERVER_ERROR;
    if (ngx_nonblocking(fd) == -1) { ngx_close_socket(fd); return NGX_HTTP_INTERNAL_SERVER_ERROR; }

    ngx_connection_t *pc = ngx_get_connection(fd, r->connection->log);
    if (pc == NULL) { ngx_close_socket(fd); return NGX_HTTP_INTERNAL_SERVER_ERROR; }

    ngx_http_ws_deflate_upstream_ctx_t *ctx;
    ctx = ngx_pcalloc(r->pool, sizeof(*ctx));
    if (ctx == NULL) return NGX_ERROR;

    ctx->backend = pc;
    ctx->buf = ngx_create_temp_buf(r->pool, 4096);
    if (ctx->buf == NULL) return NGX_ERROR;

    pc->data = r;
    pc->pool = r->pool;
    pc->log = r->connection->log;
    pc->sendfile = 0;
    pc->read->handler = ngx_ws_upstream_send_request;
    pc->write->handler = ngx_ws_upstream_send_request;

    ngx_http_set_ctx(r, ctx, ngx_http_ws_deflate_module);

    /* Connect */
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

    /* Build upgrade request WITHOUT Sec-WebSocket-Extensions */
    u_char  req_buf[4096];
    u_char *req_end = ngx_snprintf(req_buf, sizeof(req_buf),
        "GET %V HTTP/1.1\r\n"
        "Host: %V:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %V\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        &path, &host, port, &ws_key);

    size_t req_len = req_end - req_buf;
    ngx_memcpy(ctx->buf->start, req_buf, req_len);
    ctx->buf->last = ctx->buf->start + req_len;

    /* Register write event */
    ngx_handle_write_event(pc->write, 0);
    r->write_event_handler = ngx_http_request_empty_handler;

    return NGX_DONE;
}


static void
ngx_ws_upstream_send_request(ngx_event_t *ev)
{
    ngx_http_request_t *r = ev->data;
    ngx_http_ws_deflate_upstream_ctx_t *ctx;
    ctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (!ctx || !ctx->backend) return;

    ngx_connection_t *pc = ctx->backend;

    if (ctx->buf->pos < ctx->buf->last) {
        ngx_chain_t chain = { ctx->buf, NULL };
        if (pc->send_chain(pc, &chain, 0) == NGX_CHAIN_ERROR) {
            ngx_close_connection(pc); ctx->backend = NULL; return;
        }
        ctx->buf->pos = ctx->buf->last;
        ctx->buf->last = ctx->buf->start;
    }

    /* Switch to read response */
    ctx->state = 1;
    pc->read->handler = ngx_ws_upstream_read_response;
    ctx->buf->pos = ctx->buf->start;
    ngx_handle_read_event(pc->read, 0);
}


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
    size_t room = (ctx->buf->start + 4096) - ctx->buf->last;
    if (n > (ssize_t) room) n = room;
    ngx_memcpy(ctx->buf->last, buf, n);
    ctx->buf->last += n;

    /* Check for complete headers */
    u_char *end = (u_char *) strstr((const char *)ctx->buf->start, "\r\n\r\n");
    if (!end) { ngx_handle_read_event(pc->read, 0); return; }

    /* Check for 101 */
    if (!strstr((const char *) ctx->buf->start, "101")) {
        ngx_close_connection(pc); ctx->backend = NULL; return;
    }

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "ws_deflate: backend 101 OK");

    /* Forward response to client with extensions header.
     * We override the backend's response headers but keep the
     * Sec-WebSocket-Accept from the backend (so the key matches). */

    /* Find the accept key from backend response */
    u_char *accept = (u_char *) strstr((const char *)ctx->buf->start, "Sec-WebSocket-Accept:");
    u_char *accept_val = NULL;
    size_t  accept_len = 0;

    if (accept) {
        accept += 21;  /* skip "Sec-WebSocket-Accept: " */
        while (*accept == ' ') accept++;
        u_char *nl = (u_char *) strstr((const char *)accept, "\r\n");
        if (nl) {
            accept_val = accept;
            accept_len = nl - accept;
        }
    }

    if (!accept_val) {
        accept_val = (u_char *) "dGhlIHNhbXBsZSBub25jZQ==";
        accept_len = 28;
    }

    /* Send 101 response to client through nginx filter chain */
    r->headers_out.status = NGX_HTTP_SWITCHING_PROTOCOLS;
    r->header_only = 1;

    /* Set response headers */
    ngx_table_elt_t  *hh;

    hh = ngx_list_push(&r->headers_out.headers);
    if (hh == NULL) return;
    hh->hash = 1;
    ngx_str_set(&hh->key, "Upgrade");
    ngx_str_set(&hh->value, "websocket");

    hh = ngx_list_push(&r->headers_out.headers);
    if (hh == NULL) return;
    hh->hash = 1;
    ngx_str_set(&hh->key, "Connection");
    ngx_str_set(&hh->value, "upgrade");

    hh = ngx_list_push(&r->headers_out.headers);
    if (hh == NULL) return;
    hh->hash = 1;
    ngx_str_set(&hh->key, "Sec-WebSocket-Accept");
    hh->value.data = accept_val;
    hh->value.len = accept_len;

    hh = ngx_list_push(&r->headers_out.headers);
    if (hh == NULL) return;
    hh->hash = 1;
    ngx_str_set(&hh->key, "Sec-WebSocket-Extensions");
    ngx_str_set(&hh->value, "permessage-deflate");

    hh = ngx_list_push(&r->headers_out.headers);
    if (hh == NULL) return;
    hh->hash = 1;
    ngx_str_set(&hh->key, "X-WS-Deflate");
    ngx_str_set(&hh->value, "active");

    if (ngx_http_send_header(r) == NGX_ERROR) {
        ngx_close_connection(pc); ctx->backend = NULL; return;
    }

    /* Flush response to client and start tunnel */
    if (ngx_http_send_special(r, NGX_HTTP_FLUSH) == NGX_ERROR) {
        ngx_close_connection(pc); ctx->backend = NULL; return;
    }

    /* Start raw tunnel */
    ctx->state = 2;
    c->read->handler = ngx_ws_upstream_tunnel_read;
    c->read->data = r;
    pc->read->handler = ngx_ws_upstream_tunnel_read;
    pc->read->data = r;
    c->write->handler = ngx_ws_upstream_tunnel_write;
    pc->write->handler = ngx_ws_upstream_tunnel_write;

    ngx_handle_read_event(c->read, 0);
    ngx_handle_read_event(pc->read, 0);

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "ws_deflate: raw tunnel active");
}


/* Tunnel: read from one side, write to the other */
static void
ngx_ws_upstream_tunnel_read(ngx_event_t *ev)
{
    ngx_http_request_t *r = ev->data;
    ngx_http_ws_deflate_upstream_ctx_t *ctx;
    ctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (!ctx || !ctx->backend) return;

    ngx_connection_t *c = r->connection;
    ngx_connection_t *pc = ctx->backend;
    ngx_connection_t *src, *dst;

    /* Determine direction */
    if (ev == c->read) { src = c; dst = pc; }
    else { src = pc; dst = c; }

    u_char buf[8192];
    ssize_t n = src->recv(src, buf, sizeof(buf));
    if (n <= 0) { ngx_close_connection(pc); ctx->backend = NULL; return; }

    /* Forward */
    ngx_buf_t *b = ngx_create_temp_buf(r->pool, n);
    if (!b) return;
    ngx_memcpy(b->start, buf, n);
    b->last = b->start + n;
    b->memory = 1;
    ngx_chain_t chain = { b, NULL };

    if (dst->send_chain(dst, &chain, 0) == NGX_CHAIN_ERROR) {
        ngx_close_connection(pc); ctx->backend = NULL; return;
    }

    ngx_handle_read_event(src->read, 0);
}


static void
ngx_ws_upstream_tunnel_write(ngx_event_t *ev)
{
    ngx_handle_write_event(ev, 0);
}
