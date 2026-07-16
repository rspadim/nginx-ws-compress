#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_event.h>

#include "ngx_http_ws_deflate_tunnel.h"


/* Cross-platform microsecond timer for latency tracking.
 * Linux/macOS: clock_gettime (POSIX, microsecond resolution).
 * Windows: QueryPerformanceCounter (high-resolution). */
#if (NGX_WIN32)
static ngx_int_t ngx_ws_gettime_us(void) {
    static ngx_int_t  freq = 0;
    static ngx_int_t  freq_init = 0;
    LARGE_INTEGER  cnt, f;
    if (!freq_init) {
        QueryPerformanceFrequency(&f);
        freq = (ngx_int_t)(f.QuadPart / 1000000);
        freq_init = 1;
    }
    QueryPerformanceCounter(&cnt);
    return (ngx_int_t)(cnt.QuadPart / freq);
}
#else
#include <time.h>
static ngx_int_t ngx_ws_gettime_us(void) {
    struct timespec  ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ngx_int_t)(ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
}
#endif


/* Latency tracking (compile-time option, ON by default).
 * To disable: add -DNGX_WS_DEFLATE_NO_LATENCY to CFLAGS.
 * When enabled, each frame measures processing time in microseconds
 * and maintains a histogram for P50/P95 estimation.
 * Overhead: ~50ns per frame (gettimeofday syscall). */
#ifndef NGX_WS_DEFLATE_NO_LATENCY
#define NGX_WS_LATENCY_TRACK  1
#else
#define NGX_WS_LATENCY_TRACK  0
#endif
#include "ngx_http_ws_deflate_frame.h"
#include "ngx_http_ws_deflate_compress.h"
#include "ngx_http_ws_deflate_handshake.h"


/* Global counters for status page */
ngx_int_t  ngx_ws_deflate_total_connections;
ngx_int_t  ngx_ws_deflate_active_connections;
ngx_int_t  ngx_ws_deflate_compressed_bytes;
ngx_int_t  ngx_ws_deflate_uncompressed_bytes;
ngx_int_t  ngx_ws_deflate_frames_processed;

/* Global latency tracking (aggregated across all connections) */
ngx_int_t  ngx_ws_deflate_latency_sum;
ngx_int_t  ngx_ws_deflate_latency_min;
ngx_int_t  ngx_ws_deflate_latency_max;
ngx_int_t  ngx_ws_deflate_latency_count;
ngx_int_t  ngx_ws_deflate_latency_histogram[NGX_WS_LATENCY_BUCKETS];


/* Forward declarations */
static void ngx_http_ws_deflate_client_read_handler(ngx_event_t *ev);
static void ngx_http_ws_deflate_upstream_read_handler(ngx_event_t *ev);
static void ngx_http_ws_deflate_client_write_handler(ngx_event_t *ev);
static void ngx_http_ws_deflate_upstream_write_handler(ngx_event_t *ev);
static ngx_int_t ngx_http_ws_deflate_process_client_data(
    ngx_http_ws_deflate_tunnel_ctx_t *tctx);
static ngx_int_t ngx_http_ws_deflate_process_upstream_data(
    ngx_http_ws_deflate_tunnel_ctx_t *tctx);
static ngx_int_t ngx_http_ws_deflate_write(ngx_connection_t *c, u_char *data,
    size_t len, ngx_pool_t *pool);


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

    if (old_ctx != NULL) {
        tctx->client_deflate = old_ctx->client_deflate;
    }

    /* Allocate I/O buffers from request pool (freed when connection closes) */
    tctx->client_buf = ngx_create_temp_buf(r->pool, lcf->chunk_size);
    tctx->upstream_buf = ngx_create_temp_buf(r->pool, lcf->chunk_size);
    if (tctx->client_buf == NULL || tctx->upstream_buf == NULL) {
        ngx_ws_deflate_ctx_destroy(&tctx->compress_ctx);
        return NGX_ERROR;
    }

    /* Allocate reusable scratch buffers for compress/decompress.
     * These are allocated ONCE and reused for every frame,
     * eliminating per-frame pool allocations for long-lived connections. */
    tctx->tmp_compress = ngx_palloc(r->pool, NGX_WS_DEFLATE_BUF_SIZE);
    tctx->tmp_decompress = ngx_palloc(r->pool, NGX_WS_DEFLATE_BUF_SIZE);
    if (tctx->tmp_compress == NULL || tctx->tmp_decompress == NULL) {
        ngx_ws_deflate_ctx_destroy(&tctx->compress_ctx);
        return NGX_ERROR;
    }

    tctx->client_connection = c;
    tctx->upstream_connection = pc;
    tctx->conf = lcf;
    tctx->pool = r->pool;
    tctx->initialized = 1;

    /* Initialize latency tracking */
    tctx->latency_sum = 0;
    tctx->latency_min = NGX_MAX_UINT32_VALUE;
    tctx->latency_max = 0;

    ngx_ws_deflate_active_connections++;

    ngx_http_set_ctx(r, tctx, ngx_http_ws_deflate_module);

    c->read->data = r;
    c->read->handler = ngx_http_ws_deflate_client_read_handler;
    pc->read->data = r;
    pc->read->handler = ngx_http_ws_deflate_upstream_read_handler;

    c->write->data = r;
    c->write->handler = ngx_http_ws_deflate_client_write_handler;
    pc->write->data = r;
    pc->write->handler = ngx_http_ws_deflate_upstream_write_handler;

    c->read->ready = 1;
    pc->read->ready = 1;

    ngx_log_error(NGX_LOG_INFO, c->log, 0,
                  "ws_deflate: tunnel installed (deflate=%d, chunk=%uz)",
                  tctx->client_deflate, lcf->chunk_size);

    ngx_post_event(c->read, &ngx_posted_events);
    ngx_post_event(pc->read, &ngx_posted_events);

    return NGX_OK;
}


/* --- Write helper: send data through a connection --- */
static ngx_int_t
ngx_http_ws_deflate_write(ngx_connection_t *c, u_char *data, size_t len,
    ngx_pool_t *pool)
{
    ngx_buf_t    *b;
    ngx_chain_t   chain;

    b = ngx_create_temp_buf(pool, len);
    if (b == NULL) return NGX_ERROR;
    ngx_memcpy(b->start, data, len);
    b->last = b->start + len;
    b->memory = 1;
    chain.buf = b;
    chain.next = NULL;

    if (c->send_chain(c, &chain, 0) == NGX_CHAIN_ERROR) {
        return NGX_ERROR;
    }
    return NGX_OK;
}


/* --- Event handlers --- */

static void
ngx_http_ws_deflate_client_read_handler(ngx_event_t *ev)
{
    ngx_http_request_t                 *r = ev->data;
    ngx_http_ws_deflate_tunnel_ctx_t   *tctx;
    ngx_connection_t                   *c;
    ssize_t                             n;

    tctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (tctx == NULL || !tctx->initialized) return;

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
        if (ngx_handle_read_event(ev, 0) != NGX_OK) {
            ngx_http_ws_deflate_tunnel_close(r);
        }
        return;
    }

    tctx->client_buf->last += n;

    if (ngx_http_ws_deflate_process_client_data(tctx) != NGX_OK) {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    if (ngx_handle_read_event(ev, 0) != NGX_OK) {
        ngx_http_ws_deflate_tunnel_close(r);
    }
}


static void
ngx_http_ws_deflate_upstream_read_handler(ngx_event_t *ev)
{
    ngx_http_request_t                 *r = ev->data;
    ngx_http_ws_deflate_tunnel_ctx_t   *tctx;
    ngx_connection_t                   *pc;
    ssize_t                             n;

    tctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (tctx == NULL || !tctx->initialized) return;

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
        if (ngx_handle_read_event(ev, 0) != NGX_OK) {
            ngx_http_ws_deflate_tunnel_close(r);
        }
        return;
    }

    tctx->upstream_buf->last += n;

    if (ngx_http_ws_deflate_process_upstream_data(tctx) != NGX_OK) {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    if (ngx_handle_read_event(ev, 0) != NGX_OK) {
        ngx_http_ws_deflate_tunnel_close(r);
    }
}


/* --- Frame processing: client→upstream --- */

static ngx_int_t
ngx_http_ws_deflate_process_client_data(
    ngx_http_ws_deflate_tunnel_ctx_t *tctx)
{
    u_char          *data, *buf;
    size_t           len, out_len;
    ngx_ws_frame_t   frame;
    ngx_int_t        rc;
    ngx_log_t       *log;

    log = tctx->client_connection->log;

    /* Pass-through: no compression negotiated, just forward raw bytes */
    if (!tctx->client_deflate) {
        len = tctx->client_buf->last - tctx->client_buf->pos;
        if (len > 0) {
            if (ngx_http_ws_deflate_write(tctx->upstream_connection,
                    tctx->client_buf->pos, len, tctx->pool) != NGX_OK)
            {
                return NGX_ERROR;
            }
            ngx_log_error(NGX_LOG_DEBUG, log, 0,
                          "ws_deflate: client→upstream %uz bytes (raw)", len);
            ngx_ws_deflate_uncompressed_bytes += len;
            ngx_ws_deflate_compressed_bytes += len;
            ngx_ws_deflate_frames_processed++;
            tctx->client_buf->pos = tctx->client_buf->last;
        }
        return NGX_OK;
    }

    data = tctx->client_buf->pos;
    len = tctx->client_buf->last - tctx->client_buf->pos;

    while (len > 0) {
        rc = ngx_ws_frame_parse(data, len, &frame);
        if (rc == NGX_AGAIN) break;
        if (rc == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "ws_deflate: invalid frame from client");
            return NGX_ERROR;
        }

        size_t  wire_size = frame.header_len + frame.payload_len;

        /* Unmask client frames */
        if (frame.masked) {
            ngx_ws_frame_apply_mask(frame.payload, frame.payload_len,
                                    frame.masking_key);
        }

        /* Decompress if RSV1 set and it's a data frame */
        if (frame.rsv1
            && (frame.opcode == NGX_WS_OPCODE_TEXT
                || frame.opcode == NGX_WS_OPCODE_BINARY))
        {
            /* Use reusable buffer; fall back to pool alloc for large payloads */
            size_t  need = frame.payload_len * 2 + 64;
            u_char *decomp = (need <= NGX_WS_DEFLATE_BUF_SIZE)
                             ? tctx->tmp_decompress
                             : ngx_palloc(tctx->pool, need);
            if (decomp == NULL) return NGX_ERROR;

            size_t  decomp_len = need;
            if (ngx_ws_deflate_decompress(&tctx->compress_ctx,
                                           frame.payload, frame.payload_len,
                                           decomp, &decomp_len) != NGX_OK)
            {
                ngx_log_error(NGX_LOG_ERR, log, 0,
                              "ws_deflate: decompression failed");
                return NGX_ERROR;
            }

            ngx_log_error(NGX_LOG_DEBUG, log, 0,
                          "ws_deflate: decompressed %uz→%uz bytes",
                          frame.payload_len, decomp_len);
            frame.payload = decomp;
            frame.payload_len = decomp_len;
            frame.rsv1 = 0;
        }

        /* Close frame → forward and signal shutdown */
        if (frame.opcode == NGX_WS_OPCODE_CLOSE) {
            frame.masked = 0;
            out_len = frame.header_len + frame.payload_len + 14;
            buf = ngx_palloc(tctx->pool, out_len);
            if (buf == NULL) return NGX_ERROR;
            if (ngx_ws_frame_serialize(&frame, buf, &out_len) != NGX_OK) {
                return NGX_ERROR;
            }
            if (ngx_http_ws_deflate_write(tctx->upstream_connection,
                                          buf, out_len, tctx->pool) != NGX_OK)
            {
                return NGX_ERROR;
            }
            return NGX_ERROR;  /* trigger close */
        }

        /* Forward data frame to backend (raw, no mask) */
        frame.masked = 0;
        out_len = frame.header_len + frame.payload_len + 14;
        buf = tctx->tmp_compress;  /* reuse for serialization if small */
        if (out_len > NGX_WS_DEFLATE_BUF_SIZE) {
            buf = ngx_palloc(tctx->pool, out_len);
            if (buf == NULL) return NGX_ERROR;
        }
        if (ngx_ws_frame_serialize(&frame, buf, &out_len) != NGX_OK) {
            return NGX_ERROR;
        }
        if (ngx_http_ws_deflate_write(tctx->upstream_connection,
                                      buf, out_len, tctx->pool) != NGX_OK)
        {
            return NGX_ERROR;
        }

        data += wire_size;
        len -= wire_size;
    }

    /* Compact buffer */
    if (data > tctx->client_buf->pos) {
        size_t  rem = tctx->client_buf->last - data;
        if (rem > 0) ngx_memmove(tctx->client_buf->start, data, rem);
        tctx->client_buf->pos = tctx->client_buf->start;
        tctx->client_buf->last = tctx->client_buf->start + rem;
    }

    return NGX_OK;
}


/* --- Frame processing: upstream→client --- */

static ngx_int_t
ngx_http_ws_deflate_process_upstream_data(
    ngx_http_ws_deflate_tunnel_ctx_t *tctx)
{
    u_char          *data, *buf;
    size_t           len, out_len;
    ngx_ws_frame_t   frame;
    ngx_int_t        rc;
    ngx_log_t       *log;

    log = tctx->upstream_connection->log;

    /* Pass-through: no compression negotiated, just forward raw bytes */
    if (!tctx->client_deflate) {
        len = tctx->upstream_buf->last - tctx->upstream_buf->pos;
        if (len > 0) {
            if (ngx_http_ws_deflate_write(tctx->client_connection,
                    tctx->upstream_buf->pos, len, tctx->pool) != NGX_OK)
            {
                return NGX_ERROR;
            }
            ngx_log_error(NGX_LOG_DEBUG, log, 0,
                          "ws_deflate: upstream→client %uz bytes (raw)", len);
            ngx_ws_deflate_uncompressed_bytes += len;
            ngx_ws_deflate_compressed_bytes += len;  /* no compression, same size */
            ngx_ws_deflate_frames_processed++;
            tctx->upstream_buf->pos = tctx->upstream_buf->last;
        }
        return NGX_OK;
    }

    data = tctx->upstream_buf->pos;
    len = tctx->upstream_buf->last - tctx->upstream_buf->pos;

    while (len > 0) {
        rc = ngx_ws_frame_parse(data, len, &frame);
        if (rc == NGX_AGAIN) break;
        if (rc == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "ws_deflate: invalid frame from upstream");
            return NGX_ERROR;
        }

        size_t  wire_size = frame.header_len + frame.payload_len;

        /* Close frame → forward and signal shutdown */
        if (frame.opcode == NGX_WS_OPCODE_CLOSE) {
            frame.masked = 0;
            out_len = frame.header_len + frame.payload_len + 14;
            buf = ngx_palloc(tctx->pool, out_len);
            if (buf == NULL) return NGX_ERROR;
            if (ngx_ws_frame_serialize(&frame, buf, &out_len) != NGX_OK) {
                return NGX_ERROR;
            }
            if (ngx_http_ws_deflate_write(tctx->client_connection,
                                          buf, out_len, tctx->pool) != NGX_OK)
            {
                return NGX_ERROR;
            }
            return NGX_ERROR;
        }

        /* Compress data frames for the client */
        if (frame.opcode == NGX_WS_OPCODE_TEXT
            || frame.opcode == NGX_WS_OPCODE_BINARY)
        {
            ngx_int_t  t_start = 0, t_end = 0;

            if (NGX_WS_LATENCY_TRACK) {
                t_start = ngx_ws_gettime_us();
            }

            size_t  need = frame.payload_len + 64;
            u_char *comp = (need <= NGX_WS_DEFLATE_BUF_SIZE)
                           ? tctx->tmp_compress
                           : ngx_palloc(tctx->pool, need);
            if (comp == NULL) return NGX_ERROR;

            size_t  comp_len = need;
            if (ngx_ws_deflate_compress(&tctx->compress_ctx,
                                         frame.payload, frame.payload_len,
                                         comp, &comp_len) != NGX_OK)
            {
                ngx_log_error(NGX_LOG_ERR, log, 0,
                              "ws_deflate: compression failed");
                return NGX_ERROR;
            }

            if (NGX_WS_LATENCY_TRACK) {
                t_end = ngx_ws_gettime_us();
                ngx_int_t  elapsed = t_end - t_start;
                if (elapsed < 0) elapsed = 0;

                /* Update global histogram and stats */
                ngx_uint_t  bucket;
                for (bucket = 0; bucket < NGX_WS_LATENCY_BUCKETS; bucket++) {
                    if (elapsed <= (ngx_int_t) ngx_ws_latency_limits[bucket]) {
                        ngx_ws_deflate_latency_histogram[bucket]++;
                        break;
                    }
                }
                ngx_ws_deflate_latency_sum += elapsed;
                ngx_ws_deflate_latency_count++;
                if (elapsed < ngx_ws_deflate_latency_min) {
                    ngx_ws_deflate_latency_min = elapsed;
                }
                if (elapsed > ngx_ws_deflate_latency_max) {
                    ngx_ws_deflate_latency_max = elapsed;
                }
            }

            ngx_log_error(NGX_LOG_DEBUG, log, 0,
                          "ws_deflate: compressed %uz→%uz bytes",
                          frame.payload_len, comp_len);
            ngx_ws_deflate_uncompressed_bytes += frame.payload_len;
            ngx_ws_deflate_compressed_bytes += comp_len;
            ngx_ws_deflate_frames_processed++;
            frame.payload = comp;
            frame.payload_len = comp_len;
            frame.rsv1 = 1;
        }

        /* Server→client frames are NOT masked per RFC 6455 */
        frame.masked = 0;

        /* Serialize */
        out_len = frame.header_len + frame.payload_len + 14;
        buf = (out_len <= NGX_WS_DEFLATE_BUF_SIZE)
              ? tctx->tmp_compress
              : ngx_palloc(tctx->pool, out_len);
        if (buf == NULL) return NGX_ERROR;

        if (ngx_ws_frame_serialize(&frame, buf, &out_len) != NGX_OK) {
            return NGX_ERROR;
        }

        if (ngx_http_ws_deflate_write(tctx->client_connection,
                                      buf, out_len, tctx->pool) != NGX_OK)
        {
            return NGX_ERROR;
        }

        data += wire_size;
        len -= wire_size;
    }

    /* Compact buffer */
    if (data > tctx->upstream_buf->pos) {
        size_t  rem = tctx->upstream_buf->last - data;
        if (rem > 0) ngx_memmove(tctx->upstream_buf->start, data, rem);
        tctx->upstream_buf->pos = tctx->upstream_buf->start;
        tctx->upstream_buf->last = tctx->upstream_buf->start + rem;
    }

    return NGX_OK;
}


/* --- Write event handlers (no-ops — writes happen inline) --- */

static void
ngx_http_ws_deflate_client_write_handler(ngx_event_t *ev)
{
    ngx_handle_write_event(ev, 0);
}


static void
ngx_http_ws_deflate_upstream_write_handler(ngx_event_t *ev)
{
    ngx_handle_write_event(ev, 0);
}


/* --- Tunnel cleanup --- */

void
ngx_http_ws_deflate_tunnel_close(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_tunnel_ctx_t *tctx;
    tctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);

    if (tctx && tctx->initialized) {
        ngx_ws_deflate_ctx_destroy(&tctx->compress_ctx);

        ngx_ws_deflate_active_connections--;

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
