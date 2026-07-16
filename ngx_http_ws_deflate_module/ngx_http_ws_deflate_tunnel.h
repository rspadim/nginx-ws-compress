#ifndef _NGX_HTTP_WS_DEFLATE_TUNNEL_H_INCLUDED_
#define _NGX_HTTP_WS_DEFLATE_TUNNEL_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    unsigned    initialized:1;
} ngx_http_ws_deflate_ctx_t;


#endif /* _NGX_HTTP_WS_DEFLATE_TUNNEL_H_INCLUDED_ */
