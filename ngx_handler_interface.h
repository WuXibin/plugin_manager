#ifndef __NGX_HANDLER_INTERFACE_H__
#define __NGX_HANDLER_INTERFACE_H__

#if __cplusplus
extern "C" {
#endif

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

/* handler api */    
void *plugin_create_handler(void *config_file, size_t len);

ngx_int_t plugin_init_handler(void *handle); 

void plugin_destroy_handler(void *handle);

/* request api */
ngx_int_t plugin_init_request(ngx_http_request_t *r);

ngx_int_t plugin_process_request(void *handle, ngx_http_request_t *r);

ngx_int_t plugin_check_subrequest(ngx_http_request_t *r);

ngx_int_t plugin_post_subrequest(void *handle, ngx_http_request_t *r);

ngx_int_t plugin_final_request(ngx_http_request_t *r);

ngx_int_t plugin_done_request(ngx_http_request_t *r);

void plugin_destroy_request(ngx_http_request_t *r);


#if __cplusplus
}
#endif

#endif  
