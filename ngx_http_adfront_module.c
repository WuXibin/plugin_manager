#include "ngx_http_adfront_module.h"

static void *ngx_http_adfront_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_adfront_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);

static ngx_int_t ngx_http_adfront_init_process(ngx_cycle_t *cycle);

static char *ngx_http_adfront(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_adfront_handler(ngx_http_request_t *r);


static ngx_command_t  ngx_http_adfront_commands[] = {

    { ngx_string("adfront"),
      NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
      ngx_http_adfront,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("plugin_manager_config_file"),
        NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_adfront_loc_conf_t, plugin_manager_config_file);
        NULL },

    ngx_null_command
};


static ngx_http_module_t  ngx_http_adfront_module_ctx = {
    NULL,                                   /* preconfiguration */
    NULL,                                   /* postconfiguration */

    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */

    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */

    ngx_http_adfront_create_loc_conf,       /* create location configuration */
    ngx_http_adfront_merge_loc_conf         /* merge location configuration */
};


ngx_module_t  ngx_http_adfront_module = {
    NGX_MODULE_V1,
    &ngx_http_adfront_module_ctx,           /* module context */
    ngx_http_adfront_commands,              /* module directives */
    NGX_HTTP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    ngx_http_adfront_init_process,          /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};

void *adfront_handle;

static void *ngx_http_adfront_create_loc_conf(ngx_conf_t *cf) {
    ngx_http_adfront_loc_conf_t *conf;
    
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_adfront_loc_conf_t)); 
    if(conf == NULL) {
        return NGX_CONF_ERROR;
    }

    conf->plugin_manager_config_file.data = NULL;
    conf->plugin_manager_config_file.len = 0;

    return conf;
}


static char *ngx_http_adfront_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child) {
    ngx_http_adfront_loc_conf_t *conf = child;
    
    if(conf == NULL || conf->plugin_manager_config_file.len == 0) {
        return NGX_CONF_OK;
    }    

    if(adfront_handle == NULL) {
        adfront_handle = plugin_create_handler(conf->plugin_manager_config_file.data, 
                conf->plugin_manager_config_file.len);
        if(adfront_handle == NULL) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0, "adfront create handler error");
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


static ngx_int_t ngx_http_adfront_init_process(ngx_cycle_t *cycle) {
    ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0, "[adfront] init process start");
   
    if(adfront_handle == NULL) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "[adfront] handle null pointer");
        return NGX_ERROR;
    } 

    if(plugin_init_handler(adfront_handle) != 0) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "[adfront] handle init fail");
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0, "[adfront] init process success");
    return NGX_OK;
}


static char *ngx_http_adfront(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {
    ngx_http_core_loc_conf_t *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module); 
    clcf->handler = ngx_http_adfront_handler; 

    return NGX_OK;
}


static ngx_int_t ngx_http_adfront_handler(ngx_http_request_t *r) {
    ngx_int_t rc;
    ngx_http_adfront_ctx_t *ctx;
    
    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module);    
    if(ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_adfront_ctx_t));
        if(ctx == NULL) {
            return NGX_ERROR;
        }

        /*
         * set by ngx_pcalloc():
         *
         *  ctx->subrequests = NULL;
         *  ctx->plugin_ctx = NULL;
         */

        ngx_http_set_ctx(r, ctx, ngx_http_adfront_module);

        ctx->state= ADFRONT_STATE_INIT;
        gettimeofday(&ctx->time_start, NULL);
    }

    if(ctx->state == ADFRONT_STATE_INIT) {
        rc = adfront_init_request(r);

        if(rc == NGX_OK) {              //GET or POST body read completely
            ctx->state = ADFRONT_STATE_PROCESS;
        } else if(rc == NGX_AGAIN) {    //POST body read incompletely
            return NGX_DONE;
        } else {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                    "[adfront] init request error, rc = %d", rc);

            return NGX_ERROR;
        }
    }

    if(ctx->state == ADFRONT_STATE_PROCESS) {
        rc = adfront_process_request(r);

        if(rc == NGX_OK) {
            ctx->state = ADFRONT_STATE_FINAL;
        } else if(rc == NGX_AGAIN) {
            ctx->state = ADFRONT_STATE_WAIT_SUBREQUEST;

            r->main->count++;
            return NGX_DONE;
        } else {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                    "[adfront] process request error, rc = %d", rc);

            return NGX_ERROR;
        }
    }

    if(ctx->state == ADFRONT_STATE_WAIT_SUBREQUEST) {
        rc = adfront_check_subrequest(r);

        if(rc == NGX_OK) {
            ctx->state = ADFRONT_STATE_POST_SUBREQUEST;
        } else if(rc == NGX_AGAIN) {
            r->main->count++;

            return NGX_DONE;
        } 
    }

    if(ctx->state == ADFRONT_STATE_POST_SUBREQUEST) {
        rc = adfront_post_subrequest(r); 

        if(rc == NGX_OK) {
            ctx->state = ADFRONT_STATE_FINAL;
        } else if(rc == NGX_AGAIN) {
            r->main->count++;

            return NGX_DONE;
        } else {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                    "[adfront] process request error, rc = %d", rc);

            return NGX_ERROR;
        }
    }   

    if(ctx->state == ADFRONT_STATE_FINAL) {
        ctx->state = ADFRONT_STATE_DONE; 
        gettimeofday(&ctx->time_end, NULL);
        
        ngx_int_t ts = ctx->time_end.tv_sec- ctx->time_start.tv_sec;
        ngx_int_t tms = (ctx->time_end.tv_usec - ctx->time_start.tv_usec) / 1000;
        if(tms < 0) {
            ts--;
            tms += 1000;
        }


        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
                "[adfront] final request, time consume: %d.%d", ts, tms);

        return adfront_final_request(r);
    }

    if(ctx->state == ADFRONT_STATE_DONE) {
        return adfront_done_process(r); 
    }
}


static ngx_int_t adfront_init_request(ngx_http_request_t *r) {
    ngx_int_t rc;

    if(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD)) {
        return NGX_OK;
    } 

    if(r->method & NGX_HTTP_POST) {
        return ngx_http_read_client_request_body(r, adfront_post_body);
    } 
}


static ngx_int_t adfront_process_request(ngx_http_request_t *r) {
    return plugin_process_request(adfront_handle, r); 
}


static ngx_int_t adfront_check_subrequest(ngx_http_request_t *r) {
    size_t                  i, n;
    subrequest_t            *sr;
    ngx_http_adfront_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    sr = ctx->subrequests->elts; 
    n = ctx->subrequests->nelts;
    
    for(i = 0; i < n; ++i, ++sr) {
        if(sr->done != 1) {
            r->main->count++;

            return NGX_DONE;
        } 
    }

    return NGX_OK;
}

static ngx_int_t adfront_post_subrequest(ngx_http_request_t *r) {
    return plugin_post_subrequest(adfront_handle, r); 
}

static ngx_int_t adfront_final_request(ngx_http_request_t *r) {
    ngx_int_t               rc;
    ngx_uint_t              len;
    ngx_buf_t               *b;
    ngx_chain_t             out;
    ngx_http_adfront_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    if(ctx->result == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                "[adfront] result null pointer");

        return NGX_ERROR;
    } 
    
    out.buf = ctx->handle_result;
    out.next = NULL;

    if(r->headers_out.status != NGX_HTTP_MOVED_TEMPORARILY)
       r->headers_out.status = NGX_HTTP_OK; 

    r->headers_out.content_length_n = ctx->handle_result->last - ctx->handle_result->pos;

    rc = ngx_http_send_header(r);
    if(rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        ngx_log_error(NGX_LOG_ERR, r->connection, 0, 
                "[adfront] send header fail");

        return rc;
    }    

    return ngx_http_output_filter(r, &out);
}

static ngx_int_t adfront_done_process(ngx_http_request_t *r) {
    return ngx_http_output_filter(r, NULL);
}

static void adfront_post_body(ngx_http_request_t *r) {
    ngx_http_adfront_ctx_t *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    if(ctx->state == ADFRONT_STATE_INIT) {
        ngx_http_finalize_request(r, NGX_DONE);

        return;
    }

    ctx->state = ADFRONT_STATE_PROCESS;

    ngx_http_finalize_request(r, r->content_handler(r));
    ngx_http_run_posted_requests(r);

    return;    
}
