#include "ngx_handler.h"
#include "ngx_handler_interface.h"
#include "ngx_http_adfront_module.h"
#include <sharelib/pluginmanager/http_request_define.h>

#include <stdint.h>
#include <string>
#include <iostream>

using namespace std;
using namespace ngx_handler;

#define PLUGIN_MANAGER_HOME_PATH "__plugin_manager_home_path__"

static string ngx_http_get_cookie(ngx_http_request_t *r);
static string ngx_http_get_forward(ngx_http_request_t* r);
static string ngx_http_get_referer(ngx_http_request_t* r);
static string ngx_http_get_user_agent(ngx_http_request_t* r);
static string ngx_http_get_ip(ngx_http_request_t* r);

static int ngx_do_get_post_body(ngx_http_request_t *r, STR_MAP& query_map);
static void ngx_query_map_print(ngx_http_request_t* r, const STR_MAP &query_map);
int ngx_url_parser(const char *in_buffer, STR_MAP& q_map, const char *const delims);
static int ngx_header_handler(ngx_http_request_t* r, STR_MAP &query_map);

static ngx_http_request_t *cur_request;

void *plugin_create_handler(void *config_file, ngx_int_t len) {
    STR_MAP config_map;
    
    Handler *request_handler = new Handler();
    if(request_handler == NULL) {
        return NULL;
    }

    string plugin_conf_file = string((char*)config_file, len);
    config_map[PLUGIN_MANAGER_HOME_PATH] = plugin_conf_file;

    request_handler->Init(config_map);

    return request_handler;
}


void plugin_destroy_handler(void *request_handler) {
    if(request_handler != NULL) {
        ((Handler *)request_handler)->Destroy();
        delete ((Handler *)request_handler);

        request_handler = NULL;
    }
}


ngx_int_t plugin_init_handler(void *request_handler) {
    if(request_handler == NULL) {
        return NGX_OK;
    }

    return ((Handler *)request_handler)->InitProcess();
}


ngx_int_t plugin_process_request(void *request_handler, ngx_http_request_t* r) {
    ngx_int_t   rc;
    ngx_buf_t   *b;
    ngx_http_adfront_ctx_t *ctx;

    string      result;
    STR_MAP     *query_map;
    STR_MAP     kv_out;

    if (request_handler == NULL) {
        return -1; 
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_adfront_module); 
    ctx->handle_map = new STR_MAP();
    query_map = (STR_MAP *)ctx->handle_map;

    ngx_header_handler(r, *query_map);
    ngx_do_get_post_body(r, *query_map);
    
    ngx_query_map_print(r, *query_map);

    rc = ((Handler *)request_handler)->Handle(*query_map, kv_out, result);

    if(rc == NGX_OK) {
        b = ngx_create_temp_buf(r->pool, result.length()); 
        if(b == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        ngx_memcpy(b->pos, result.c_str(), result.length());
        b->last = b->pos + result.length();
        b->last_buf = 1;
    }

    return rc; 
}

// Get cookie string from nginx request struct.
static string ngx_http_get_cookie(ngx_http_request_t *r)
{
    ngx_table_elt_t ** cookies = NULL;
    ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0, 
        "Cookie count: %d\n", r->headers_in.cookies.nelts);
    cookies = (ngx_table_elt_t**)r->headers_in.cookies.elts;
    ngx_uint_t i = 0;
    for (i = 0; i < r->headers_in.cookies.nelts; i++) { 
        ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0,
            "Cookie line %d: %s\n", i, cookies[i]->value.data);
    }
    if (r->headers_in.cookies.nelts > 0) {
        return string((char*)cookies[0]->value.data, cookies[0]->value.len);
    }
    return "";

}

static string ngx_http_get_forward(ngx_http_request_t* r)
{
    if (NULL == r->headers_in.x_forwarded_for){
        return "";
    }
    if (r->headers_in.x_forwarded_for->value.len > 0) {
        return string((char*)r->headers_in.x_forwarded_for->value.data ,
            r->headers_in.x_forwarded_for->value.len);
    }
    return "";
}

static string ngx_http_get_referer(ngx_http_request_t* r)
{
    if (NULL == r->headers_in.referer){
        return "";
    }
    if (r->headers_in.referer->value.len > 0) {
        return string((char*)r->headers_in.referer->value.data ,
            r->headers_in.referer->value.len);
    }
    return "";
}


static string ngx_http_get_user_agent(ngx_http_request_t* r)
{
    if (NULL == r->headers_in.user_agent) {
        return "";
    }
    if (r->headers_in.user_agent->value.len > 0) {
        return string((char*)r->headers_in.user_agent->value.data ,
            r->headers_in.user_agent->value.len);
    }
    return "";
}

static string ngx_http_get_ip(ngx_http_request_t* r)
{
    if (r->headers_in.x_forwarded_for == NULL) {
        if (r->headers_in.x_real_ip == NULL) {
          //  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "x_real_ip == NULL");
            if (r->connection->addr_text.len > 0) 
                return string((char*)r->connection->addr_text.data, r->connection->addr_text.len);
            return "";
        }
        if (r->headers_in.x_real_ip->value.len > 0) {
            return string((char*)r->headers_in.x_real_ip->value.data ,
                r->headers_in.x_real_ip->value.len);
        }
    } else {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,"x_forward_for : %s", 
                    (char*)r->headers_in.x_forwarded_for->value.data);
        if (r->headers_in.x_forwarded_for->value.len >0) {
            string forward_ip((char*)r->headers_in.x_forwarded_for->value.data, 
                                     r->headers_in.x_forwarded_for->value.len);
            std::size_t found = forward_ip.find(',');
            if (found!=std::string::npos)
                return string(forward_ip, 0, found);
            else return forward_ip;
        }
    }
    return "";
}

// This function process nginx request struct for post type.
static int ngx_do_get_post_body(ngx_http_request_t *r, STR_MAP& query_map)
{
    if (!(r->method & (NGX_HTTP_POST))) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,"HTTP Get request accepted");
        return RET_OK;
    }
    size_t len = 0; //post data len   
    u_char *buf = NULL, *p = NULL, *last = NULL; //post data pointer   
    ngx_chain_t *cl;
    if (r->request_body != NULL && r->request_body->bufs != NULL) {
        for (cl = r->request_body->bufs; cl; cl = cl->next) {
            if(ngx_buf_in_memory(cl->buf)){
                len += cl->buf->last - cl->buf->pos;
            }else{
                len += cl->buf->file_last - cl->buf->file_pos;
                ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
                    "cl->buf->file_last - cl->buf->file_pos: %d", cl->buf->file_last - cl->buf->file_pos);
            }	        
        }
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "len: %d", len);
        if (len > 0) {
            buf = (u_char*)ngx_pcalloc(r->pool, len + 1);
            if (buf == NULL) {
                ngx_http_finalize_request(r, NGX_ERROR);
                return RET_OK;
            }
            p = buf;
            last = p + len; 

            for (cl = r->request_body->bufs; cl; cl = cl->next) {
                if(ngx_buf_in_memory(cl->buf)){
                    p = ngx_copy(p, cl->buf->pos, cl->buf->last - cl->buf->pos);
                }else{
                    if(NULL != cl->buf->file){
                        if(0 > ngx_read_file(cl->buf->file, p, cl->buf->file_last - cl->buf->file_pos, cl->buf->file_pos)){
                            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "ngx_read_file failed");
                            return RET_OK;
                        }						
                        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "len: %lu", len);
                    }					
                }				
            }
        }
    }	else {
        return RET_OK;
    }
    query_map[HTTP_REQUEST_POST_BODY] = string((char*)buf);
    return RET_OK;
}

static void ngx_query_map_print(ngx_http_request_t* r, const STR_MAP &query_map)
{
    STR_MAP::const_iterator iter = query_map.begin();
    while (iter != query_map.end()) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, "key=%s,value=%s", 
            iter->first.c_str(), iter->second.c_str());
        iter++;
    }
}

// Parse http url string and save q_map, split by =
int ngx_url_parser(const char *in_buffer, STR_MAP& q_map, const char *const delims)
{
    char *key = NULL;
    size_t key_len =0;
    char *value = NULL;
    size_t value_len = 0;
    char *p = NULL;
    if(NULL != in_buffer){
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
    }
    return RET_OK;
}

// Process nginx request string  and save these value into map.
static int ngx_header_handler(ngx_http_request_t* r, STR_MAP &query_map)
{
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

