#ifndef _NGX_HTTP_WS_DEFLATE_HANDSHAKE_H_INCLUDED_
#define _NGX_HTTP_WS_DEFLATE_HANDSHAKE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    ngx_flag_t    enabled;
    ngx_flag_t    auto_detect;
    ngx_array_t  *except_patterns;
    ngx_int_t     compression_level;
    ngx_flag_t    context_takeover;
    size_t        chunk_size;
} ngx_http_ws_deflate_loc_conf_t;


extern ngx_module_t ngx_http_ws_deflate_module;

ngx_int_t ngx_http_ws_deflate_handshake_handler(ngx_http_request_t *r);
ngx_int_t ngx_http_ws_deflate_request_handler(ngx_http_request_t *r);


#endif /* _NGX_HTTP_WS_DEFLATE_HANDSHAKE_H_INCLUDED_ */
