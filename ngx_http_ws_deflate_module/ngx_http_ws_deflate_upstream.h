/*
 * WebSocket compression bridge - direct upstream handler.
 * Bypasses the proxy module entirely.
 */

#ifndef _NGX_HTTP_WS_DEFLATE_UPSTREAM_H_INCLUDED_
#define _NGX_HTTP_WS_DEFLATE_UPSTREAM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/* Global upstream_pass string — set during config parse, read by upstream handler */
extern ngx_str_t  ngx_ws_upstream_pass;


ngx_int_t ngx_http_ws_deflate_upstream_handler(ngx_http_request_t *r);


#endif /* _NGX_HTTP_WS_DEFLATE_UPSTREAM_H_INCLUDED_ */
