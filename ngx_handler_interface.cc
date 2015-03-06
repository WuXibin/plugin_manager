#include "ngx_handler.h"
#include "ngx_handler_interface.h"
#include "ngx_http_adfront_module.h"

#include <stdint.h>
#include <string>
#include <iostream>

using namespace std;
using namespace ngx_handler;
using namespace sharelib;

static string ngx_http_get_cookie(ngx_http_request_t *r);
static string ngx_http_get_forward(ngx_http_request_t* r);
static string ngx_http_get_referer(ngx_http_request_t* r);
static string ngx_http_get_user_agent(ngx_http_request_t* r);
static string ngx_http_get_ip(ngx_http_request_t* r);

static int ngx_url_parser(const char *in_buffer, STR_MAP& q_map, const char *delims);

static int ngx_header_handler(ngx_http_request_t* r, STR_MAP &query_map);
static int ngx_do_get_post_body(ngx_http_request_t *r, STR_MAP& query_map);
static void ngx_query_map_print(ngx_http_request_t* r, const STR_MAP &query_map);

static ngx_int_t plugin_start_subrequest(ngx_http_request_t *r);
static ngx_int_t plugin_create_ctx(ngx_http_request_t *r);
static void plugin_destroy_ctx(ngx_http_request_t *r);


/*------------------------------ handler api ---------------------------------*/
void *plugin_create_handler(char *config_file, size_t len) {
    Handler *request_handler = new Handler();
    if(request_handler == NULL) {
        return NULL;
    }

    string conf_file = string(config_file, len);
    request_handler->Init(conf_file);

    return request_handler;
}


ngx_int_t plugin_init_handler(void *request_handler) {
    if(request_handler == NULL) {
        return NGX_OK;
    }

    return ((Handler *)request_handler)->InitProcess();
}


void plugin_destroy_handler(void *request_handler) {
    if(request_handler != NULL) {
        ((Handler *)request_handler)->Destroy();
        delete ((Handler *)request_handler);

        request_handler = NULL;
    }
}


/*--------------------------------- request api ------------------------------*/

/*
 * @return 
 *      NGX_OK      plugin process request sucess   
 *      NGX_AGAIN   plugin has subrequest to be processed
 *      NGX_ERROR   plugin process reuquest fail
 */
ngx_int_t plugin_process_request(void *request_handler, ngx_http_request_t* r) {
    ngx_int_t rc;
    ngx_http_adfront_ctx_t *ctx;

    /* create context for each http request exactly once */
    rc = plugin_create_ctx(r);
    if(rc != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                "[adfront] plugin create context error");

        return NGX_ERROR;
    }
        
    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    IPluginCtx *plugin_ctx = (IPluginCtx *)ctx->plugin_ctx;

    rc = ((Handler *)request_handler)->Handle(plugin_ctx);

    if(rc == PLUGIN_AGAIN) {
        if(plugin_ctx->subrequest_uri_.empty()) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "[adfront] plugin return again without subrequest uri");
            
            return NGX_ERROR;
        }

        return plugin_start_subrequest(r);
    }

    /* assert(rc = PLUGIN_DONE) */
    return NGX_OK;
}

/*
 * @return 
 *      NGX_OK          all subrequests have been done
 *      NGX_AGAIN       some subrequests haven't been done
 */
ngx_int_t plugin_check_subrequest(ngx_http_request_t *r) {
    size_t                  n;
    ngx_buf_t               *b;
    subrequest_t            *st;
    ngx_http_adfront_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    IPluginCtx *plugin_ctx = (IPluginCtx *)ctx->plugin_ctx;

    st = ctx->subrequests->elts; 
    n = ctx->subrequests->nelts;
    for(size_t i = 0; i < n; i++, st++) {
        if(st->subr->done != 1) {
            return NGX_AGAIN;
        } 
    }

    plugin_ctx->handle_result_.clear();
    st = ctx->subrequests->elts; 
    for(size_t i = 0; i < n; i++) {
        b = &st->subr->upstream->buffer; 

        string res = string((char *)b->pos, b->last - b->pos); 
        plugin_ctx->subrequest_res_.push_back(res); 
    }

    return NGX_OK;
}


/*
 * @return 
 *      NGX_OK      plugin process request sucess   
 *      NGX_AGAIN   plugin has subrequest to be processed
 *      NGX_ERROR   plugin process reuquest fail
 */
ngx_int_t plugin_post_subrequest(void *request_handler, ngx_http_request_t *r) {
    ngx_int_t rc;
    ngx_http_adfront_ctx_t *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    IPluginCtx *plugin_ctx = (IPluginCtx *)ctx->plugin_ctx;

    rc = ((Handler *)request_handler)->PostSubHandle(plugin_ctx);

    if(rc == PLUGIN_AGAIN) {
        if(plugin_ctx->subrequest_uri_.empty()) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "[adfront] plugin return again without subrequest uri");
            
            return NGX_ERROR;
        }

        return plugin_start_subrequest(r);
    } 

    /* assert(rc = PLUGIN_DONE) */
    return NGX_OK;
}


/*
 * @return 
 *      NGX_OK          output filter body complete
 *      NGX_AGAIN       output filter body incomplete
 *      NGX_ERROR       plugin finalize request fail
 */
ngx_int_t plugin_finalize_request(ngx_http_request_t *r) {
    ngx_int_t               rc;
    ngx_buf_t               *b;
    ngx_chain_t             out;
    ngx_http_adfront_ctx_t  *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    IPluginCtx *plugin_ctx = (IPluginCtx *)ctx->plugin_ctx;
    
    if(plugin_ctx->handle_result_.empty()) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                "[adfront] plugin finalize with empty result");

        plugin_destroy_ctx(r);
        return NGX_ERROR;
    }

    b = ngx_create_temp_buf(r->pool, plugin_ctx->handle_result_.length()); 
    if(b == NULL) {
        plugin_destroy_ctx(r);
        return NGX_ERROR;
    }

    ngx_memcpy(b->pos, plugin_ctx->handle_result_.c_str(), 
            plugin_ctx->handle_result_.length());
    b->last = b->pos + plugin_ctx->handle_result_.length();
    b->last_buf = 1;

    out.buf = b;
    out.next = NULL;

    if(r->headers_out.status != NGX_HTTP_MOVED_TEMPORARILY)
       r->headers_out.status = NGX_HTTP_OK; 

    ngx_str_t type = ngx_string("text/plain; charset=utf-8");
    r->headers_out.content_type = type;
    r->headers_out.content_length_n = plugin_ctx->handle_result_.length();

    rc = ngx_http_send_header(r);
    if(rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                "[adfront] send header fail");

        plugin_destroy_ctx(r);
        return NGX_ERROR;
    }    

    plugin_destroy_ctx(r);
    return ngx_http_output_filter(r, &out);
}

void plugin_destroy_request(ngx_http_request_t *r) {
    plugin_destroy_ctx(r); 
}

/*---------------------------- local function --------------------------------*/
static string ngx_http_get_cookie(ngx_http_request_t *r) {
    ngx_table_elt_t **cookies = NULL;

    ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0, 
        "Cookie count: %d\n", r->headers_in.cookies.nelts);

    cookies = (ngx_table_elt_t**)r->headers_in.cookies.elts;

    ngx_uint_t i = 0;
    for (i = 0; i < r->headers_in.cookies.nelts; i++) { 
        ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0,
            "Cookie line %d: %s\n", i, cookies[i]->value.data);
    }

    if (r->headers_in.cookies.nelts > 0) {
        return string((char *)cookies[0]->value.data, cookies[0]->value.len);
    }

    return "";
}

static string ngx_http_get_forward(ngx_http_request_t* r) {
    if (NULL == r->headers_in.x_forwarded_for) {
        return "";
    }

    if (r->headers_in.x_forwarded_for->value.len > 0) {
        return string((char *)r->headers_in.x_forwarded_for->value.data ,
            r->headers_in.x_forwarded_for->value.len);
    }

    return "";
}

static string ngx_http_get_referer(ngx_http_request_t* r) {
    if (NULL == r->headers_in.referer){
        return "";
    }

    if (r->headers_in.referer->value.len > 0) {
        return string((char *)r->headers_in.referer->value.data ,
            r->headers_in.referer->value.len);
    }

    return "";
}


static string ngx_http_get_user_agent(ngx_http_request_t* r) {
    if (NULL == r->headers_in.user_agent) {
        return "";
    }

    if (r->headers_in.user_agent->value.len > 0) {
        return string((char *)r->headers_in.user_agent->value.data ,
            r->headers_in.user_agent->value.len);
    }

    return "";
}

static string ngx_http_get_ip(ngx_http_request_t* r) {
    if (r->headers_in.x_forwarded_for == NULL) {
        if (r->headers_in.x_real_ip == NULL) {
            if (r->connection->addr_text.len > 0) 
                return string((char*)r->connection->addr_text.data, 
                        r->connection->addr_text.len);
            return "";
        }

        if (r->headers_in.x_real_ip->value.len > 0) {
            return string((char*)r->headers_in.x_real_ip->value.data ,
                r->headers_in.x_real_ip->value.len);
        }
    } else {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,"x_forward_for : %s", 
                    (char*)r->headers_in.x_forwarded_for->value.data);

        if (r->headers_in.x_forwarded_for->value.len > 0) {
            string forward_ip((char*)r->headers_in.x_forwarded_for->value.data, 
                                     r->headers_in.x_forwarded_for->value.len);

            std::size_t found = forward_ip.find(',');
            if (found != std::string::npos)
                return string(forward_ip, 0, found);
            else 
                return forward_ip;
        }
    }

    return "";
}


static int ngx_do_get_post_body(ngx_http_request_t *r, STR_MAP& query_map) {
    if (!(r->method & (NGX_HTTP_POST))) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                "[adfront] http get request accepted");

        return NGX_OK;
    }

    
    /* NGX_HTTP_POST */
    //TODO
    
    return NGX_OK;
}


static void ngx_query_map_print(ngx_http_request_t* r, const STR_MAP &query_map) {
    STR_MAP::const_iterator iter = query_map.begin();

    for(; iter != query_map.end(); iter++) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
                "[adfront] key=%s, value=%s", 
                iter->first.c_str(), iter->second.c_str());
    }
}


static int ngx_url_parser(const char *in_buffer, STR_MAP& q_map, const char *delims) {
    char *key = NULL;
    size_t key_len =0;
    char *value = NULL;
    size_t value_len = 0;
    char *p = NULL;

    if(NULL != in_buffer){
        return NGX_OK;
    }
        char *buffer = (char *)in_buffer;
        //char *buffer = (char*)malloc(strlen(in_buffer) + 1);
        //memcpy(buffer, in_buffer, strlen(in_buffer) + 1);
        for (p = strchr(buffer, '='); p; p = strchr(p, '=')) {
            if (p == buffer) {
                ++p;
                continue;
            }
            for (key = p-1; isspace(*key); --key);
            key_len = 0;
            while (isalnum(*key) || '_' == *key || '\\' == *key || '/' == *key || ':' == *key) {
                /* don't parse backwards off the start of the string */
                if (key == buffer) {
                    --key;
                    ++key_len;
                    break;
                }
                --key;
                ++key_len;
            }
            ++key;
            *(buffer + (key - buffer) + key_len) = '\0';
            for (value = p+1; isspace(*value); ++value);
            value_len = strcspn(value, delims);
            p = value + value_len;
            if ('\0' != *p){
                *(value + value_len) = '\0';
                p = value + value_len + 1;    
            }else{
                p = value + value_len;
            }
            q_map.insert(make_pair(key, value));
        }
    return RET_OK;
}

static int ngx_header_handler(ngx_http_request_t* r, STR_MAP &query_map) {
    string raw_str = string((char*)r->args.data, r->args.len);

    string tmp_str;
    string tmp_url_str;

    if (0 != UrlDecode(raw_str, tmp_str)) {
        return -1;
    }

    Replace_all(tmp_str,"\t"," ");
    query_map[HTTP_REQUEST_URL] = tmp_str;
    tmp_url_str = tmp_str;
    string delstr = "?&platform";
    {   
        size_t pos_url = tmp_url_str.find(delstr);
        ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0,
                "before tmp_url_str=%s pos_url=%lu", tmp_url_str.c_str(),pos_url);
        if (pos_url != string::npos ){
            tmp_url_str = tmp_url_str.substr(0,pos_url); 
            ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0,
                "after temp_url_str=%s", tmp_url_str.c_str());
        }   
    }

    //if (RET_OK != ngx_url_parser(tmp_str.c_str(), query_map,"&")) {
    if (RET_OK != ngx_url_parser(tmp_url_str.c_str(), query_map,"&")) {
        return -1;
    }
    tmp_str = string((char*)r->unparsed_uri.data, r->unparsed_uri.len);
    ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0,
        "urllen=%lu,url=%s", r->unparsed_uri.len, tmp_str.c_str());
    size_t pos1 = tmp_str.find_last_of("/"), pos2 = tmp_str.find_first_of("?");
    ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0, 
        "pos1=%lu, pos2=%lu", pos1,pos2);
    if (pos1 != string::npos && pos2 != string::npos && pos2 >pos1) {
        tmp_str = tmp_str.substr(pos1+1, pos2-pos1-1);
    }
    else {
        if (pos1 != string::npos) {
            tmp_str = tmp_str.substr(pos1+1);
        }
        else if(pos2 != string::npos){
            tmp_str = tmp_str.substr(0, pos2);
        }
    }
    query_map[HTTP_REQUEST_PLUGINNAME] = tmp_str;
    tmp_str = ngx_http_get_cookie(r); 
    Replace_all(tmp_str,"\t"," ");
    query_map[HTTP_REQUEST_COOKIE] = tmp_str;
    if (r->method & (NGX_HTTP_POST)) {
        query_map[HTTP_REQUEST_METHOD] = HTTP_REQUEST_POST_METHOD; 
    }
    else if(r->method & (NGX_HTTP_GET)){
        query_map[HTTP_REQUEST_METHOD] = HTTP_REQUEST_GET_METHOD;
    }
    tmp_str = ngx_http_get_forward(r); 
    Replace_all(tmp_str,"\t"," ");
    query_map[HTTP_REQUEST_HEADER_FORWARD]= tmp_str;
    //query_map[HTTP_REQUEST_HEADER_FORWARD]= ngx_http_get_forward(r);

    tmp_str = ngx_http_get_user_agent(r); 
    //tmp_str = "test1	test2	test3";
    ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0, 
            "ngx_http_get_user_agent input:%s",tmp_str.c_str() );
    Replace_all(tmp_str,"\t"," ");
    ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0, 
            "ngx_http_get_user_agent output:%s",tmp_str.c_str() );
    query_map[HTTP_REQUEST_HEADER_USER_AGENT]= tmp_str;
    //query_map[HTTP_REQUEST_HEADER_USER_AGENT]= ngx_http_get_user_agent(r);

    tmp_str = ngx_http_get_referer(r); 
    Replace_all(tmp_str,"\t"," ");
    query_map[HTTP_REQUEST_HEADER_REFERER] = tmp_str;
    //query_map[HTTP_REQUEST_HEADER_REFERER] = ngx_http_get_referer(r);

    tmp_str = ngx_http_get_ip(r); 
    Replace_all(tmp_str,"\t"," ");
    query_map["userip"] = tmp_str;
    ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0, "------------userip : %s\n", query_map["userip"].c_str());

    return RET_OK;
}

static ngx_int_t plugin_start_subrequest(ngx_http_request_t *r) {
    size_t n;
    subrequest_t *st;
    ngx_http_adfront_ctx_t *ctx;
    ngx_http_post_subrequest_t *psr;

    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    IPluginCtx *plugin_ctx = (IPluginCtx *)ctx->plugin_ctx;

    /* destroy subrequests created before */
    if(ctx->subrequests) {
        ngx_array_destroy(ctx->subrequests);
    }
    ctx->subrequests = ngx_array_create(r->pool, 1, sizeof(subrequest_t));

    n = plugin_ctx->subrequest_uri_.size();
    for(size_t i = 0; i < n; i++) {
        string url = plugin_ctx->subrequest_uri_[i]; 

        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
                "[adfront] plugin start subrequest %s", url.c_str());

        size_t pos = url.find('?'); 

        if(pos == string::npos) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                    "[adfront] invalid subrequest uri %s", url.c_str());
            
            return NGX_ERROR;
        }

        string str_uri = url.substr(0, pos);
        string str_args = url.substr(pos + 1);
        
        ngx_str_t uri = ngx_string(str_uri.c_str());
        ngx_str_t args = ngx_string(str_args.c_str());

        st = ngx_array_push(ctx->subrequests); 
        if(st == NULL) {
            return NGX_ERROR;
        }

        st->uri.data = ngx_pcalloc(r->pool, uri.len);
        if(st->uri.data == NULL) {
            return NGX_ERROR;
        }
        ngx_memcpy(st->uri.data, uri.data, uri.len); 
        st->uri.len = uri.len;
        
        st->args.data = ngx_pcalloc(r->pool, args.len);
        if(st->args.data == NULL) {
            return NGX_ERROR;
        }
        ngx_memcpy(st->args.data, args.data, args.len); 
        st->args.len = args.len;
    }

    /* plugin_ctx->subrequest_uri_.clear(); */
    
    n = ctx->subrequests->nelts;
    st = ctx->subrequests->elts;
    for(size_t i = 0; i < n; i++, st++) {
        int flags = NGX_HTTP_SUBREQUEST_IN_MEMORY | NGX_HTTP_SUBREQUEST_WAITED;

        psr = ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
        if(psr == NULL) {
            return NGX_ERROR;
        }

        psr->handler = plugin_subrequest_post_handler;
        psr->data = NULL;

        ngx_int_t rc = ngx_http_subrequest(r, &st->uri, &st->args, 
                &st->subr, psr, flags);

        if(rc != NGX_OK) 
            return NGX_ERROR;
    }

    return NGX_AGAIN;
}


static ngx_int_t
plugin_subrequest_post_handler(ngx_http_request_t *r, void *data, ngx_int_t rc) {
    r->parent->write_event_handler = ngx_http_core_run_phases;

    return NGX_OK;
}

static ngx_int_t plugin_create_ctx(ngx_http_request_t *r) {
    ngx_http_adfront_ctx_t *ctx;

    IPluginCtx *plugin_ctx = new IPluginCtx();
    if(plugin_ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_header_handler(r, plugin_ctx->headers_in_);
    ngx_do_get_post_body(r, plugin_ctx->headers_in_);
    
    ngx_query_map_print(r, plugin_ctx->headers_in_);

    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    ctx->plugin_ctx = plugin_ctx;
   
    return NGX_OK;
}

static void plugin_destroy_ctx(ngx_http_request_t *r) {
    ngx_http_adfront_ctx_t *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module);

    if(ctx->plugin_ctx)
        delete (IPluginCtx *)ctx->plugin_ctx;
}
