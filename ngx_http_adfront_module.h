#ifndef __NGX_HTTP_ADFRONT_MODULE_H__
#define __NGX_HTTP_ADFRONT_MODULE_H__

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <sys/time.h>

typedef enum {
    ADFRONT_STATE_INIT,
    ADFRONT_STATE_PROCESS,
    ADFRONT_STATE_WAIT_SUBREQUEST,
    ADFRONT_STATE_POST_SUBREQUEST,
    ADFRONT_STATE_FINAL,
    ADFRONT_STATE_DONE,
    ADFRONT_STATE_ERROR
} adfront_state_t;


typedef struct {
    //ngx_int_t           done
    ngx_str_t           uri;
    ngx_str_t           args;
    ngx_http_request_t  *subr;   
} subrequest_t;


typedef struct {
    ngx_str_t   plugin_manager_config_file; 
} ngx_http_adfront_loc_conf_t;


typedef struct {
    struct timeval      time_start;
    struct timeval      time_end;
    adfront_state_t     state;
    ngx_array_t         *subrequests;
    
    void                *plugin_ctx;
    //ngx_buf_t           *plugin_res;
} ngx_http_adfront_ctx_t;

extern ngx_module_t  ngx_http_adfront_module;

#endif
