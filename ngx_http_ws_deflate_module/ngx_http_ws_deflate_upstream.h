/*
 * WebSocket compression bridge - direct upstream handler.
 * Bypasses the proxy module entirely.
 */

#ifndef _NGX_HTTP_WS_DEFLATE_UPSTREAM_H_INCLUDED_
#define _NGX_HTTP_WS_DEFLATE_UPSTREAM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/* Set upstream_pass URL from config directive (called during config parse) */
void ngx_ws_upstream_set_pass(const u_char *data, size_t len);


ngx_int_t ngx_http_ws_deflate_upstream_handler(ngx_http_request_t *r);


#endif /* _NGX_HTTP_WS_DEFLATE_UPSTREAM_H_INCLUDED_ */
