#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_event.h>

#include "ngx_http_ws_deflate_tunnel.h"
#include "ngx_http_ws_deflate_frame.h"
#include "ngx_http_ws_deflate_compress.h"
#include "ngx_http_ws_deflate_handshake.h"

/* TODO: Per-frame pool allocations (decomp/compress/serialize buffers) grow
 * the request pool unboundedly for long-lived WebSocket connections.
 * Future optimization: use a reusable scratch buffer or a per-frame sub-pool
 * that can be freed after each frame. */


/* Forward declarations */
static void ngx_http_ws_deflate_client_read_handler(ngx_event_t *ev);
static void ngx_http_ws_deflate_upstream_read_handler(ngx_event_t *ev);
static void ngx_http_ws_deflate_client_write_handler(ngx_event_t *ev);
static void ngx_http_ws_deflate_upstream_write_handler(ngx_event_t *ev);
static ngx_int_t ngx_http_ws_deflate_process_client_data(
    ngx_http_ws_deflate_tunnel_ctx_t *tctx);
static ngx_int_t ngx_http_ws_deflate_process_upstream_data(
    ngx_http_ws_deflate_tunnel_ctx_t *tctx);


ngx_int_t
ngx_http_ws_deflate_tunnel_install(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_tunnel_ctx_t  *tctx, *old_ctx;
    ngx_http_ws_deflate_loc_conf_t    *lcf;
    ngx_connection_t                  *c, *pc;

    c = r->connection;
    if (r->upstream == NULL || r->upstream->peer.connection == NULL) {
        return NGX_ERROR;
    }
    pc = r->upstream->peer.connection;

    lcf = ngx_http_get_module_loc_conf(r, ngx_http_ws_deflate_module);

    /* Check if there's an existing context (from handshake) */
    old_ctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);

    tctx = ngx_pcalloc(r->pool, sizeof(ngx_http_ws_deflate_tunnel_ctx_t));
    if (tctx == NULL) {
        return NGX_ERROR;
    }

    if (ngx_ws_deflate_ctx_init(&tctx->compress_ctx, lcf->compression_level,
                                lcf->context_takeover) != NGX_OK)
    {
        return NGX_ERROR;
    }

    /* Preserve client_deflate flag from handshake context if present */
    if (old_ctx != NULL) {
        tctx->client_deflate = old_ctx->client_deflate;
    }

    /* Allocate buffers using the request pool */
    tctx->client_buf = ngx_create_temp_buf(r->pool, lcf->chunk_size);
    tctx->upstream_buf = ngx_create_temp_buf(r->pool, lcf->chunk_size);
    if (tctx->client_buf == NULL || tctx->upstream_buf == NULL) {
        ngx_ws_deflate_ctx_destroy(&tctx->compress_ctx);
        return NGX_ERROR;
    }

    tctx->client_connection = c;
    tctx->upstream_connection = pc;
    tctx->conf = lcf;
    tctx->pool = r->pool;
    tctx->initialized = 1;

    ngx_http_set_ctx(r, tctx, ngx_http_ws_deflate_module);

    /* Replace read handlers */
    c->read->data = r;
    c->read->handler = ngx_http_ws_deflate_client_read_handler;
    pc->read->data = r;
    pc->read->handler = ngx_http_ws_deflate_upstream_read_handler;

    /* Replace write handlers (used for event notification) */
    c->write->data = r;
    c->write->handler = ngx_http_ws_deflate_client_write_handler;
    pc->write->data = r;
    pc->write->handler = ngx_http_ws_deflate_upstream_write_handler;

    /* Disable nginx's post-upgrade buffering */
    c->read->ready = 1;
    pc->read->ready = 1;

    ngx_log_error(NGX_LOG_INFO, c->log, 0,
                  "ws_deflate: tunnel installed for WebSocket connection");

    /* Trigger initial reads */
    ngx_post_event(c->read, &ngx_posted_events);
    ngx_post_event(pc->read, &ngx_posted_events);

    return NGX_OK;
}


static void
ngx_http_ws_deflate_client_read_handler(ngx_event_t *ev)
{
    ngx_http_request_t                 *r = ev->data;
    ngx_http_ws_deflate_tunnel_ctx_t   *tctx;
    ngx_connection_t                   *c;
    ssize_t                             n;

    tctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (tctx == NULL || !tctx->initialized) {
        return;
    }

    c = tctx->client_connection;

    if (ev->timedout) {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    n = c->recv(c, tctx->client_buf->last,
                 tctx->client_buf->end - tctx->client_buf->last);

    if (n == NGX_ERROR || n == 0) {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    if (n == NGX_AGAIN) {
        ngx_handle_read_event(ev, 0);
        return;
    }

    tctx->client_buf->last += n;

    if (ngx_http_ws_deflate_process_client_data(tctx) != NGX_OK) {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    ngx_handle_read_event(ev, 0);
}


static void
ngx_http_ws_deflate_upstream_read_handler(ngx_event_t *ev)
{
    ngx_http_request_t                 *r = ev->data;
    ngx_http_ws_deflate_tunnel_ctx_t   *tctx;
    ngx_connection_t                   *pc;
    ssize_t                             n;

    tctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (tctx == NULL || !tctx->initialized) {
        return;
    }

    pc = tctx->upstream_connection;

    if (ev->timedout) {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    n = pc->recv(pc, tctx->upstream_buf->last,
                 tctx->upstream_buf->end - tctx->upstream_buf->last);

    if (n == NGX_ERROR || n == 0) {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    if (n == NGX_AGAIN) {
        ngx_handle_read_event(ev, 0);
        return;
    }

    tctx->upstream_buf->last += n;

    if (ngx_http_ws_deflate_process_upstream_data(tctx) != NGX_OK) {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    ngx_handle_read_event(ev, 0);
}


static ngx_int_t
ngx_http_ws_deflate_process_client_data(
    ngx_http_ws_deflate_tunnel_ctx_t *tctx)
{
    /* Process client→upstream: decompress frames from client, forward raw to backend */
    u_char          *data, *write_buf;
    size_t           len, write_len;
    ngx_ws_frame_t   frame;
    ngx_int_t        rc;

    /* Pass-through: when client_deflate is disabled, just forward raw bytes */
    if (!tctx->client_deflate) {
        len = tctx->client_buf->last - tctx->client_buf->pos;
        if (len > 0) {
            ngx_connection_t *pc = tctx->upstream_connection;
            ngx_buf_t *b = ngx_create_temp_buf(tctx->pool, len);
            if (b == NULL) return NGX_ERROR;
            ngx_memcpy(b->start, tctx->client_buf->pos, len);
            b->last = b->start + len;
            b->memory = 1;
            ngx_chain_t chain;
            chain.buf = b;
            chain.next = NULL;
            if (pc->send_chain(pc, &chain, 0) == NGX_CHAIN_ERROR) {
                return NGX_ERROR;
            }
            tctx->client_buf->pos = tctx->client_buf->last;
        }
        return NGX_OK;
    }

    data = tctx->client_buf->pos;
    len = tctx->client_buf->last - tctx->client_buf->pos;

    while (len > 0) {
        rc = ngx_ws_frame_parse(data, len, &frame);
        if (rc == NGX_AGAIN) {
            break;  /* Need more data */
        }
        if (rc == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, tctx->client_connection->log, 0,
                          "ws_deflate: invalid frame from client");
            return NGX_ERROR;
        }

        /* Capture wire size BEFORE modifying frame */
        size_t  wire_size = frame.header_len + frame.payload_len;

        /* Unmask if needed (client frames are masked) */
        if (frame.masked) {
            ngx_ws_frame_apply_mask(frame.payload, frame.payload_len,
                                    frame.masking_key);
        }

        /* If this is a data frame with RSV1 set, decompress it (only if deflate negotiated) */
        if (tctx->client_deflate
            && frame.rsv1
            && (frame.opcode == NGX_WS_OPCODE_TEXT
                || frame.opcode == NGX_WS_OPCODE_BINARY))
        {
            /* Allocate decompression buffer */
            u_char  *decomp = ngx_palloc(tctx->pool, frame.payload_len * 2 + 64);
            if (decomp == NULL) return NGX_ERROR;

            size_t  decomp_len = frame.payload_len * 2 + 64;
            if (ngx_ws_deflate_decompress(&tctx->compress_ctx,
                                           frame.payload, frame.payload_len,
                                           decomp, &decomp_len) != NGX_OK)
            {
                ngx_log_error(NGX_LOG_ERR, tctx->client_connection->log, 0,
                              "ws_deflate: decompression failed");
                return NGX_ERROR;
            }

            frame.payload = decomp;
            frame.payload_len = decomp_len;
            frame.rsv1 = 0;  /* Clear compression flag for backend */
        }

        /* Handle close frames (opcode 0x8) */
        if (frame.opcode == NGX_WS_OPCODE_CLOSE) {
            /* Forward the close frame to upstream, then close */
            frame.masked = 0;
            write_len = frame.header_len + frame.payload_len + 14;
            write_buf = ngx_palloc(tctx->pool, write_len);
            if (write_buf == NULL) return NGX_ERROR;
            rc = ngx_ws_frame_serialize(&frame, write_buf, &write_len);
            if (rc != NGX_OK) return NGX_ERROR;

            ngx_connection_t *pc = tctx->upstream_connection;
            ngx_buf_t *b = ngx_create_temp_buf(tctx->pool, write_len);
            if (b == NULL) return NGX_ERROR;
            ngx_memcpy(b->start, write_buf, write_len);
            b->last = b->start + write_len;
            b->memory = 1;
            ngx_chain_t  chain_out;
            chain_out.buf = b;
            chain_out.next = NULL;
            if (pc->send_chain(pc, &chain_out, 0) == NGX_CHAIN_ERROR) {
                return NGX_ERROR;
            }
            /* Return error to trigger tunnel close */
            return NGX_ERROR;
        }

        /* Clear mask flag for forwarding to backend (server frames are not masked) */
        frame.masked = 0;

        /* Serialize the frame for upstream */
        write_len = frame.header_len + frame.payload_len + 14; /* enough */
        write_buf = ngx_palloc(tctx->pool, write_len);
        if (write_buf == NULL) return NGX_ERROR;

        rc = ngx_ws_frame_serialize(&frame, write_buf, &write_len);
        if (rc != NGX_OK) return NGX_ERROR;

        /* Write to upstream */
        {
            ngx_connection_t *pc = tctx->upstream_connection;
            ngx_buf_t *b = ngx_create_temp_buf(tctx->pool, write_len);
            if (b == NULL) return NGX_ERROR;
            ngx_memcpy(b->start, write_buf, write_len);
            b->last = b->start + write_len;
            b->memory = 1;

            ngx_chain_t  chain_out;
            chain_out.buf = b;
            chain_out.next = NULL;

            if (pc->send_chain(pc, &chain_out, 0) == NGX_CHAIN_ERROR) {
                return NGX_ERROR;
            }
        }

        /* Advance buffer by wire_size (before decompression changed payload_len) */
        data += wire_size;
        len -= wire_size;
    }

    /* Compact buffer */
    if (data > tctx->client_buf->pos) {
        size_t  remaining = tctx->client_buf->last - data;
        if (remaining > 0) {
            ngx_memmove(tctx->client_buf->start, data, remaining);
        }
        tctx->client_buf->pos = tctx->client_buf->start;
        tctx->client_buf->last = tctx->client_buf->start + remaining;
    }

    return NGX_OK;
}



static ngx_int_t
ngx_http_ws_deflate_process_upstream_data(
    ngx_http_ws_deflate_tunnel_ctx_t *tctx)
{
    /* Process upstream→client: compress data frames from backend, forward to client */
    u_char          *data, *write_buf;
    size_t           len, write_len;
    ngx_ws_frame_t   frame;
    ngx_int_t        rc;

    /* Pass-through: when client_deflate is disabled, just forward raw bytes */
    if (!tctx->client_deflate) {
        len = tctx->upstream_buf->last - tctx->upstream_buf->pos;
        if (len > 0) {
            ngx_connection_t *c = tctx->client_connection;
            ngx_buf_t *b = ngx_create_temp_buf(tctx->pool, len);
            if (b == NULL) return NGX_ERROR;
            ngx_memcpy(b->start, tctx->upstream_buf->pos, len);
            b->last = b->start + len;
            b->memory = 1;
            ngx_chain_t chain;
            chain.buf = b;
            chain.next = NULL;
            if (c->send_chain(c, &chain, 0) == NGX_CHAIN_ERROR) {
                return NGX_ERROR;
            }
            tctx->upstream_buf->pos = tctx->upstream_buf->last;
        }
        return NGX_OK;
    }

    data = tctx->upstream_buf->pos;
    len = tctx->upstream_buf->last - tctx->upstream_buf->pos;

    while (len > 0) {
        rc = ngx_ws_frame_parse(data, len, &frame);
        if (rc == NGX_AGAIN) {
            break;
        }
        if (rc == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, tctx->upstream_connection->log, 0,
                          "ws_deflate: invalid frame from upstream");
            return NGX_ERROR;
        }

        /* Capture wire size BEFORE modifying frame */
        size_t  wire_size = frame.header_len + frame.payload_len;

        /* Upstream frames are never masked, no unmask needed */

        /* Handle close frames */
        if (frame.opcode == NGX_WS_OPCODE_CLOSE) {
            /* Forward to client, then close */
            frame.masked = 0;
            write_len = frame.header_len + frame.payload_len + 14;
            write_buf = ngx_palloc(tctx->pool, write_len);
            if (write_buf == NULL) return NGX_ERROR;
            rc = ngx_ws_frame_serialize(&frame, write_buf, &write_len);
            if (rc != NGX_OK) return NGX_ERROR;

            ngx_connection_t *c = tctx->client_connection;
            ngx_buf_t *b = ngx_create_temp_buf(tctx->pool, write_len);
            if (b == NULL) return NGX_ERROR;
            ngx_memcpy(b->start, write_buf, write_len);
            b->last = b->start + write_len;
            b->memory = 1;
            ngx_chain_t  chain_out;
            chain_out.buf = b;
            chain_out.next = NULL;
            if (c->send_chain(c, &chain_out, 0) == NGX_CHAIN_ERROR) {
                return NGX_ERROR;
            }
            return NGX_ERROR;  /* Signal close */
        }

        /* Compress data frames only if client requested deflate */
        if (tctx->client_deflate
            && (frame.opcode == NGX_WS_OPCODE_TEXT
                || frame.opcode == NGX_WS_OPCODE_BINARY))
        {
            u_char  *comp = ngx_palloc(tctx->pool, frame.payload_len + 64);
            if (comp == NULL) return NGX_ERROR;

            size_t  comp_len = frame.payload_len + 64;
            if (ngx_ws_deflate_compress(&tctx->compress_ctx,
                                         frame.payload, frame.payload_len,
                                         comp, &comp_len) != NGX_OK)
            {
                ngx_log_error(NGX_LOG_ERR, tctx->upstream_connection->log, 0,
                              "ws_deflate: compression failed");
                return NGX_ERROR;
            }

            frame.payload = comp;
            frame.payload_len = comp_len;
            frame.rsv1 = 1;  /* Mark as compressed for client */
        }
        /* Control frames (ping/pong) pass through unchanged */

        /* Server→client frames are NOT masked per RFC 6455 */
        frame.masked = 0;

        /* Serialize for client */
        write_len = frame.header_len + frame.payload_len + 14;
        write_buf = ngx_palloc(tctx->pool, write_len);
        if (write_buf == NULL) return NGX_ERROR;

        rc = ngx_ws_frame_serialize(&frame, write_buf, &write_len);
        if (rc != NGX_OK) return NGX_ERROR;

        /* Write to client */
        {
            ngx_connection_t *c = tctx->client_connection;
            ngx_buf_t *b = ngx_create_temp_buf(tctx->pool, write_len);
            if (b == NULL) return NGX_ERROR;
            ngx_memcpy(b->start, write_buf, write_len);
            b->last = b->start + write_len;
            b->memory = 1;

            ngx_chain_t  chain_out;
            chain_out.buf = b;
            chain_out.next = NULL;

            if (c->send_chain(c, &chain_out, 0) == NGX_CHAIN_ERROR) {
                return NGX_ERROR;
            }
        }

        /* Advance buffer by wire_size (before compression changed payload_len) */
        data += wire_size;
        len -= wire_size;
    }

    /* Compact buffer */
    if (data > tctx->upstream_buf->pos) {
        size_t  remaining = tctx->upstream_buf->last - data;
        if (remaining > 0) {
            ngx_memmove(tctx->upstream_buf->start, data, remaining);
        }
        tctx->upstream_buf->pos = tctx->upstream_buf->start;
        tctx->upstream_buf->last = tctx->upstream_buf->start + remaining;
    }

    return NGX_OK;
}


static void
ngx_http_ws_deflate_client_write_handler(ngx_event_t *ev)
{
    /* Client write events handled inline in process_upstream_data */
    ngx_http_request_t *r = ev->data;
    ngx_http_ws_deflate_tunnel_ctx_t *tctx;
    tctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (tctx == NULL) return;

    ngx_handle_write_event(ev, 0);
}


static void
ngx_http_ws_deflate_upstream_write_handler(ngx_event_t *ev)
{
    /* Upstream write events handled inline in process_client_data */
    ngx_handle_write_event(ev, 0);
}


void
ngx_http_ws_deflate_tunnel_close(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_tunnel_ctx_t *tctx;
    tctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);

    if (tctx && tctx->initialized) {
        ngx_ws_deflate_ctx_destroy(&tctx->compress_ctx);

        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                      "ws_deflate: closing tunnel");

        if (tctx->upstream_connection
            && !tctx->upstream_connection->error)
        {
            ngx_close_connection(tctx->upstream_connection);
        }

        if (tctx->client_connection
            && !tctx->client_connection->error)
        {
            ngx_close_connection(tctx->client_connection);
        }

        tctx->initialized = 0;
    }

    ngx_http_finalize_request(r, NGX_OK);
}
