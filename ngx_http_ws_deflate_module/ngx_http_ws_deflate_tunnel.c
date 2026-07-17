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


/* Debug helper: hex dump up to 48 bytes into a static buffer. */
#define NGX_WS_HEX_BUF_SIZE  128
static u_char *ngx_ws_hex(const u_char *data, size_t len) {
    static u_char  buf[NGX_WS_HEX_BUF_SIZE];
    u_char        *p = buf;
    size_t         i, n = (len < 48) ? len : 48;
    for (i = 0; i < n; i++) {
        p = ngx_sprintf(p, "%02xd", data[i]);
    }
    if (len > 48) { p = ngx_sprintf(p, "..."); }
    *p = '\0';
    return buf;
}


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
static void ngx_http_ws_deflate_read_upstream(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_ws_deflate_write_upstream(ngx_http_request_t *r,
    ngx_http_upstream_t *u);
static void ngx_http_ws_deflate_read_downstream(ngx_http_request_t *r);
static void ngx_http_ws_deflate_write_downstream(ngx_http_request_t *r);
static ngx_int_t ngx_http_ws_deflate_process_data(
    ngx_http_ws_deflate_tunnel_ctx_t *tctx,
    ngx_connection_t *src, ngx_connection_t *dst,
    ngx_buf_t *buf, ngx_flag_t from_upstream);
static ngx_int_t ngx_http_ws_deflate_write(ngx_connection_t *c, u_char *data,
    size_t len, ngx_pool_t *pool);


ngx_int_t
ngx_http_ws_deflate_tunnel_install(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_tunnel_ctx_t  *tctx, *old_ctx;
    ngx_http_ws_deflate_loc_conf_t    *lcf;
    ngx_http_upstream_t               *u;

    u = r->upstream;
    if (u == NULL || u->peer.connection == NULL) {
        return NGX_ERROR;
    }

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

    /* Allocate reusable scratch buffers for compress/decompress */
    tctx->tmp_compress = ngx_palloc(r->pool, NGX_WS_DEFLATE_BUF_SIZE);
    tctx->tmp_decompress = ngx_palloc(r->pool, NGX_WS_DEFLATE_BUF_SIZE);
    if (tctx->tmp_compress == NULL || tctx->tmp_decompress == NULL) {
        ngx_ws_deflate_ctx_destroy(&tctx->compress_ctx);
        return NGX_ERROR;
    }

    tctx->client_connection = r->connection;
    tctx->upstream_connection = u->peer.connection;
    tctx->conf = lcf;
    tctx->pool = r->pool;
    tctx->initialized = 1;

    /* Initialize latency tracking */
    tctx->latency_sum = 0;
    tctx->latency_min = NGX_MAX_UINT32_VALUE;
    tctx->latency_max = 0;

    ngx_ws_deflate_active_connections++;

    ngx_http_set_ctx(r, tctx, ngx_http_ws_deflate_module);

    /*
     * Replace upstream event handlers instead of connection handlers.
     * Connection handlers stay as ngx_http_upstream_handler (set by proxy
     * module), which then dispatches to u->read_event_handler /
     * u->write_event_handler.  By replacing these function pointers we
     * intercept the data flow without conflicting with the proxy module's
     * event management.
     */
    u->read_event_handler = ngx_http_ws_deflate_read_upstream;
    u->write_event_handler = ngx_http_ws_deflate_write_upstream;

    /* Also set request-level handlers for downstream events */
    r->read_event_handler = ngx_http_ws_deflate_read_downstream;
    r->write_event_handler = ngx_http_ws_deflate_write_downstream;

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "ws_deflate: tunnel installed (deflate=%d, chunk=%uz)",
                  tctx->client_deflate, lcf->chunk_size);

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


/* --- Upstream event handler: processes data FROM upstream, writes TO client --- */
/* Called by ngx_http_upstream_handler for read events on BOTH connections.
 * Also called by ngx_http_run_posted_requests indirectly.
 * We read from upstream, compress, and write to client. */

static void
ngx_http_ws_deflate_read_upstream(ngx_http_request_t *r,
    ngx_http_upstream_t *u)
{
    ngx_http_ws_deflate_tunnel_ctx_t   *tctx;
    ngx_connection_t                   *pc;
    ssize_t                             n;

    tctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (tctx == NULL || !tctx->initialized) {
        return;
    }

    pc = u->peer.connection;

    if (pc->read->timedout) {
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
        if (ngx_handle_read_event(pc->read, 0) != NGX_OK) {
            ngx_http_ws_deflate_tunnel_close(r);
        }
        return;
    }

    tctx->upstream_buf->last += n;

    /* Process upstream→client: compress data frames */
    if (ngx_http_ws_deflate_process_data(tctx, pc, r->connection,
            tctx->upstream_buf, 1) != NGX_OK)
    {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    if (ngx_handle_read_event(pc->read, 0) != NGX_OK) {
        ngx_http_ws_deflate_tunnel_close(r);
    }
}


static void
ngx_http_ws_deflate_write_upstream(ngx_http_request_t *r,
    ngx_http_upstream_t *u)
{
    ngx_http_ws_deflate_tunnel_ctx_t   *tctx;

    tctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (tctx == NULL || !tctx->initialized) {
        return;
    }

    /* Write events on the upstream connection mean the upstream
     * write buffer is ready.  Read upstream data (if any) and
     * forward to client — same as read_upstream. */
    ngx_http_ws_deflate_read_upstream(r, u);
}


/* --- Request-level write handler: processes data FROM client, writes TO upstream --- */
/* Called by ngx_http_run_posted_requests after ngx_http_upstream_handler.
 * We read from client, decompress, and write to upstream. */

static void
ngx_http_ws_deflate_write_downstream(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_tunnel_ctx_t   *tctx;
    ngx_http_upstream_t                *u;
    ngx_connection_t                   *c;
    ssize_t                             n;

    tctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (tctx == NULL || !tctx->initialized) {
        return;
    }

    u = r->upstream;
    if (u == NULL) return;

    c = r->connection;

    ngx_log_error(NGX_LOG_DEBUG, c->log, 0,
                  "ws_deflate: write_downstream called");

    n = c->recv(c, tctx->client_buf->last,
                tctx->client_buf->end - tctx->client_buf->last);

    if (n == NGX_ERROR || n == 0) {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    if (n == NGX_AGAIN) {
        if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
            ngx_http_ws_deflate_tunnel_close(r);
        }
        return;
    }

    tctx->client_buf->last += n;

    /* Process client→upstream: decompress frames with RSV1=1 */
    if (ngx_http_ws_deflate_process_data(tctx, c, u->peer.connection,
            tctx->client_buf, 0) != NGX_OK)
    {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
        ngx_http_ws_deflate_tunnel_close(r);
    }
}


/* --- Request-level read handler (deferred client reads) --- */

static void
ngx_http_ws_deflate_read_downstream(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_tunnel_ctx_t   *tctx;
    ngx_http_upstream_t                *u;
    ngx_connection_t                   *c;
    ssize_t                             n;

    tctx = ngx_http_get_module_ctx(r, ngx_http_ws_deflate_module);
    if (tctx == NULL || !tctx->initialized) {
        return;
    }

    u = r->upstream;
    if (u == NULL) return;

    c = r->connection;

    n = c->recv(c, tctx->client_buf->last,
                tctx->client_buf->end - tctx->client_buf->last);

    if (n == NGX_ERROR || n == 0) {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    if (n == NGX_AGAIN) {
        if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
            ngx_http_ws_deflate_tunnel_close(r);
        }
        return;
    }

    tctx->client_buf->last += n;

    if (ngx_http_ws_deflate_process_data(tctx, c, u->peer.connection,
            tctx->client_buf, 0) != NGX_OK)
    {
        ngx_http_ws_deflate_tunnel_close(r);
        return;
    }

    if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
        ngx_http_ws_deflate_tunnel_close(r);
    }
}


/* --- Unified frame processing: handles both directions --- */

static ngx_int_t
ngx_http_ws_deflate_process_data(
    ngx_http_ws_deflate_tunnel_ctx_t *tctx,
    ngx_connection_t *src, ngx_connection_t *dst,
    ngx_buf_t *buf, ngx_flag_t from_upstream)
{
    u_char          *data, *out_buf;
    size_t           len, out_len;
    ngx_ws_frame_t   frame;
    ngx_int_t        rc;
    ngx_log_t       *log;

    log = src->log;

    ngx_log_error(NGX_LOG_DEBUG, log, 0,
                  "ws_deflate: process_data from_upstream=%d client_deflate=%d len=%uz",
                  (int) from_upstream, (int) tctx->client_deflate,
                  buf->last - buf->pos);

    /* Pass-through: no compression, just forward raw bytes */
    if (!tctx->client_deflate) {
        len = buf->last - buf->pos;
        if (len > 0) {
            if (ngx_http_ws_deflate_write(dst, buf->pos, len, tctx->pool) != NGX_OK) {
                return NGX_ERROR;
            }
            ngx_log_error(NGX_LOG_DEBUG, log, 0,
                          "ws_deflate: %s→%s %uz bytes (raw)",
                          from_upstream ? "upstream" : "client",
                          from_upstream ? "client" : "upstream", len);
            buf->pos = buf->last;
        }
        return NGX_OK;
    }

    data = buf->pos;
    len = buf->last - buf->pos;

    /* Debug: hex dump frame data when ws_deflate_debug is on */
    if (tctx->conf->debug && len > 0) {
        ngx_log_error(NGX_LOG_NOTICE, log, 0,
                      "ws_deflate: %s buf (%uz bytes): %s",
                      from_upstream ? "upstream" : "client",
                      len, ngx_ws_hex(data, len));
    }

    while (len > 0) {
        rc = ngx_ws_frame_parse(data, len, &frame);
        if (rc == NGX_AGAIN) break;
        if (rc == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "ws_deflate: invalid frame from %s",
                          from_upstream ? "upstream" : "client");
            return NGX_ERROR;
        }

        size_t  wire_size = frame.header_len + frame.payload_len;

        /* Unmask client frames (only client→upstream direction) */
        if (!from_upstream && frame.masked) {
            ngx_ws_frame_apply_mask(frame.payload, frame.payload_len,
                                    frame.masking_key);
        }

        if (from_upstream) {
            /* Upstream→client:
             * - RSV1=0 (raw from backend) → compress with our settings
             * - RSV1=1 (backend already compressed) → pass through as-is */
            if (frame.rsv1
                && (frame.opcode == NGX_WS_OPCODE_TEXT
                    || frame.opcode == NGX_WS_OPCODE_BINARY))
            {
                ngx_log_error(NGX_LOG_DEBUG, log, 0,
                              "ws_deflate: upstream frame already compressed "
                              "(%uz bytes, pass through)", frame.payload_len);
                /* Keep frame.rsv1 = 1, forward as-is */

            } else if (frame.opcode == NGX_WS_OPCODE_TEXT
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
                          "ws_deflate: compressed %uz→%uz bytes (fu=%d)",
                          frame.payload_len, comp_len, from_upstream);
                ngx_ws_deflate_uncompressed_bytes += frame.payload_len;
                ngx_ws_deflate_compressed_bytes += comp_len;
                ngx_ws_deflate_frames_processed++;
                frame.payload = comp;
                frame.payload_len = comp_len;
                frame.rsv1 = 1;
            }

        } else {
            /* Client→upstream: decompress if RSV1 set */
            if (frame.rsv1
                && (frame.opcode == NGX_WS_OPCODE_TEXT
                    || frame.opcode == NGX_WS_OPCODE_BINARY))
            {
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
        }

        /* Close frame → forward and signal shutdown */
        if (frame.opcode == NGX_WS_OPCODE_CLOSE) {
            if (!from_upstream) {
                static ngx_uint_t  close_seed = 0;
                close_seed = (close_seed + 1) * 1103515245 + 12345;
                frame.masking_key = (uint32_t) close_seed;
                frame.masked = 1;
                ngx_ws_frame_apply_mask(frame.payload, frame.payload_len,
                                        frame.masking_key);
            } else {
                frame.masked = 0;
            }
            out_len = frame.header_len + frame.payload_len + 14;
            out_buf = ngx_palloc(tctx->pool, out_len);
            if (out_buf == NULL) return NGX_ERROR;
            if (ngx_ws_frame_serialize(&frame, out_buf, &out_len) != NGX_OK) {
                return NGX_ERROR;
            }
            if (ngx_http_ws_deflate_write(dst, out_buf, out_len, tctx->pool) != NGX_OK) {
                return NGX_ERROR;
            }
            return NGX_ERROR;  /* trigger close */
        }

        /* Forward data/continuation frames */
        if (!from_upstream) {
            /* Client→upstream frames MUST be masked per RFC 6455.
             * The mask was stripped during unmask+processing;
             * generate a new mask and apply it to the payload. */
            static ngx_uint_t  mask_seed = 0;
            mask_seed = (mask_seed + 1) * 1103515245 + 12345;
            frame.masking_key = (uint32_t) mask_seed;
            frame.masked = 1;
            ngx_ws_frame_apply_mask(frame.payload, frame.payload_len,
                                    frame.masking_key);
        } else {
            frame.masked = 0;
        }
        out_len = frame.header_len + frame.payload_len + 14;
        out_buf = tctx->tmp_compress;
        if (out_len > NGX_WS_DEFLATE_BUF_SIZE) {
            out_buf = ngx_palloc(tctx->pool, out_len);
            if (out_buf == NULL) return NGX_ERROR;
        }
        if (ngx_ws_frame_serialize(&frame, out_buf, &out_len) != NGX_OK) {
            return NGX_ERROR;
        }
        if (ngx_http_ws_deflate_write(dst, out_buf, out_len, tctx->pool) != NGX_OK) {
            return NGX_ERROR;
        }

        data += wire_size;
        len -= wire_size;
    }

    /* Compact buffer */
    if (data > buf->pos) {
        size_t  rem = buf->last - data;
        if (rem > 0) ngx_memmove(buf->start, data, rem);
        buf->pos = buf->start;
        buf->last = buf->start + rem;
    }

    return NGX_OK;
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
        tctx->initialized = 0;

        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                      "ws_deflate: closing tunnel");
    }

    /* Don't close connections or finalize request — the proxy/upstream
     * module handles its own cleanup.  We just free our compression
     * context and counters. */
}
