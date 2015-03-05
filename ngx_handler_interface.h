#ifndef __NGX_HANDLER_INTERFACE_H__
#define __NGX_HANDLER_INTERFACE_H__

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#if __cplusplus
extern "C" {
#endif

    
void *plugin_create_handler(void *config_file, ngx_int_t len);

ngx_int_t plugin_init_handler(void *handle); 

ngx_int_t plugin_process_request(void *handle, ngx_http_request_t *r);

ngx_int_t plugin_post_subrequest(void *handle, ngx_http_request_t *r);

void plugin_destroy_handler(void *handle);


#if __cplusplus
}
#endif

#endif  
