#ifndef _NGX_HTTP_WS_DEFLATE_TUNNEL_H_INCLUDED_
#define _NGX_HTTP_WS_DEFLATE_TUNNEL_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_ws_deflate_compress.h"
#include "ngx_http_ws_deflate_handshake.h"


#define NGX_WS_DEFLATE_BUF_SIZE 65536  /* 64KB reusable buffers */

/* Histogram buckets for latency tracking (microseconds).
 * Bucket upper bounds: 0, 50, 100, 200, 500, 1000, 2000, 5000, 10000+
 * Each bucket counts frames whose latency falls in that range.
 * Memory: ~72 bytes per connection, no per-sample storage. */
#define NGX_WS_LATENCY_BUCKETS 9
static const ngx_uint_t  ngx_ws_latency_limits[NGX_WS_LATENCY_BUCKETS] = {
    50, 100, 200, 500, 1000, 2000, 5000, 10000, NGX_MAX_UINT32_VALUE
};


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

    /* Statistics counters for status page */
    ngx_atomic_t                 comp_in_bytes;       /* bytes compressed (original) */
    ngx_atomic_t                 comp_out_bytes;      /* bytes compressed (output) */
    ngx_atomic_t                 decomp_in_bytes;     /* bytes decompressed (original) */
    ngx_atomic_t                 decomp_out_bytes;    /* bytes decompressed (output) */
    ngx_atomic_t                 total_frames;        /* total frames processed */

    /* Latency tracking (microseconds) */
    ngx_atomic_t                 latency_sum;         /* sum of all latencies for mean */
    ngx_atomic_t                 latency_min;         /* minimum observed */
    ngx_atomic_t                 latency_max;         /* maximum observed */
    /* Histogram buckets for latency percentiles (not atomic — best-effort stats) */
    ngx_int_t  latency_histogram[NGX_WS_LATENCY_BUCKETS];  /* per-frame latency buckets */
} ngx_http_ws_deflate_tunnel_ctx_t;


ngx_int_t ngx_http_ws_deflate_tunnel_install(ngx_http_request_t *r);
void ngx_http_ws_deflate_tunnel_close(ngx_http_request_t *r);

/* Global counters for status page (defined in tunnel.c) */
extern ngx_int_t  ngx_ws_deflate_total_connections;
extern ngx_int_t  ngx_ws_deflate_active_connections;
extern ngx_int_t  ngx_ws_deflate_compressed_bytes;
extern ngx_int_t  ngx_ws_deflate_uncompressed_bytes;
extern ngx_int_t  ngx_ws_deflate_frames_processed;

/* Global latency tracking */
extern ngx_int_t  ngx_ws_deflate_latency_sum;
extern ngx_int_t  ngx_ws_deflate_latency_min;
extern ngx_int_t  ngx_ws_deflate_latency_max;
extern ngx_int_t  ngx_ws_deflate_latency_count;
extern ngx_int_t  ngx_ws_deflate_latency_histogram[NGX_WS_LATENCY_BUCKETS];


#endif /* _NGX_HTTP_WS_DEFLATE_TUNNEL_H_INCLUDED_ */
