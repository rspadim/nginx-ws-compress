#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_ws_deflate_handshake.h"


static ngx_int_t ngx_http_ws_deflate_postconfiguration(ngx_conf_t *cf);
static void *ngx_http_ws_deflate_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_ws_deflate_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_ws_deflate_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static ngx_int_t ngx_http_ws_deflate_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_ws_deflate_content_handler(ngx_http_request_t *r);

static char *ngx_http_ws_deflate_except_slot(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static ngx_http_output_header_filter_pt ngx_http_next_header_filter;


static ngx_command_t ngx_http_ws_deflate_commands[] = {

    { ngx_string("ws_deflate"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ws_deflate_loc_conf_t, enabled),
      NULL },

    { ngx_string("ws_deflate_auto"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ws_deflate_loc_conf_t, auto_detect),
      NULL },

    { ngx_string("ws_deflate_compression_level"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ws_deflate_loc_conf_t, compression_level),
      NULL },

    { ngx_string("ws_deflate_context_takeover"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ws_deflate_loc_conf_t, context_takeover),
      NULL },

    { ngx_string("ws_deflate_chunk_size"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ws_deflate_loc_conf_t, chunk_size),
      NULL },

    { ngx_string("ws_deflate_max_compress_len"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ws_deflate_loc_conf_t, max_compress_len),
      NULL },

    { ngx_string("ws_deflate_except"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE12,
      ngx_http_ws_deflate_except_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t ngx_http_ws_deflate_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_ws_deflate_postconfiguration, /* postconfiguration */
    ngx_http_ws_deflate_create_main_conf,  /* create main configuration */
    NULL,                                  /* init main configuration */
    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */
    ngx_http_ws_deflate_create_loc_conf,   /* create location configuration */
    ngx_http_ws_deflate_merge_loc_conf     /* merge location configuration */
};


ngx_module_t ngx_http_ws_deflate_module = {
    NGX_MODULE_V1,
    &ngx_http_ws_deflate_module_ctx,      /* module context */
    ngx_http_ws_deflate_commands,          /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_http_ws_deflate_postconfiguration(ngx_conf_t *cf)
{
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_handler_pt        *h;

    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_ws_deflate_header_filter;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PRECONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_ws_deflate_content_handler;

    return NGX_OK;
}


static ngx_int_t
ngx_http_ws_deflate_header_filter(ngx_http_request_t *r)
{
    if (r->upstream != NULL) {
        ngx_http_ws_deflate_handshake_handler(r);
    }

    return ngx_http_next_header_filter(r);
}


static ngx_int_t
ngx_http_ws_deflate_content_handler(ngx_http_request_t *r)
{
    ngx_http_ws_deflate_request_handler(r);

    return NGX_DECLINED;
}


static void *
ngx_http_ws_deflate_create_main_conf(ngx_conf_t *cf)
{
    return ngx_pcalloc(cf->pool, sizeof(ngx_http_ws_deflate_main_conf_t));
}


static void *
ngx_http_ws_deflate_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_ws_deflate_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_ws_deflate_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enabled = NGX_CONF_UNSET;
    conf->auto_detect = NGX_CONF_UNSET;
    conf->compression_level = NGX_CONF_UNSET;
    conf->context_takeover = NGX_CONF_UNSET;
    conf->chunk_size = NGX_CONF_UNSET_SIZE;
    conf->max_compress_len = NGX_CONF_UNSET_SIZE;

    return conf;
}


static char *
ngx_http_ws_deflate_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_ws_deflate_loc_conf_t *prev = parent;
    ngx_http_ws_deflate_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->enabled, prev->enabled, 0);
    ngx_conf_merge_value(conf->auto_detect, prev->auto_detect, 0);
    ngx_conf_merge_value(conf->compression_level, prev->compression_level, 6);
    ngx_conf_merge_value(conf->context_takeover, prev->context_takeover, 1);
    ngx_conf_merge_size_value(conf->chunk_size, prev->chunk_size, 65536);
    ngx_conf_merge_size_value(conf->max_compress_len, prev->max_compress_len, 0);

    return NGX_CONF_OK;
}


static char *
ngx_http_ws_deflate_except_slot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_ws_deflate_main_conf_t *mcf = conf;
    ngx_str_t *value;

    if (cf->args->nelts < 2) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    if (cf->args->nelts == 2) {
        /* ws_deflate_except /path */
        mcf->except_pattern = value[1];
    } else {
        /* ws_deflate_except ~ /regex/ — concatenate */
        mcf->except_pattern.len = value[1].len + 1 + value[2].len;
        mcf->except_pattern.data = ngx_pnalloc(cf->pool,
            mcf->except_pattern.len + 1);
        if (mcf->except_pattern.data == NULL) {
            return NGX_CONF_ERROR;
        }
        ngx_memcpy(mcf->except_pattern.data, value[1].data, value[1].len);
        mcf->except_pattern.data[value[1].len] = ' ';
        ngx_memcpy(mcf->except_pattern.data + value[1].len + 1,
                   value[2].data, value[2].len);
    }

    return NGX_CONF_OK;
}
