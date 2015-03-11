#include "uri_codec.h"
#include "ngx_handler.h"
#include "ngx_handler_interface.h"
#include "ngx_http_adfront_module.h"

#include <ctype.h>
#include <stdint.h>
#include <string>
#include <iostream>

using namespace std;
using namespace ngx_handler;
using namespace sharelib;

/*
 * For historical reason, our nginx may receive url like this:
 *  /uri?a=1&b=2?&paltform=android 
 * It will confuse our url parser, so we need to remove "?platform" 
 * and the characters after that. The macro below is a switcher.
 */
#define __URL_COMPAT__  1

static string ngx_http_get_cookie(ngx_http_request_t *r);
static string ngx_http_get_forward(ngx_http_request_t* r);
static string ngx_http_get_referer(ngx_http_request_t* r);
static string ngx_http_get_user_agent(ngx_http_request_t* r);
static string ngx_http_get_realip(ngx_http_request_t* r);

static int ngx_header_handler(ngx_http_request_t* r, STR_MAP &query_map);
static int ngx_do_get_post_body(ngx_http_request_t *r, STR_MAP& query_map);
static int ngx_url_parser(const string &url, STR_MAP& kv);
static void ngx_query_map_print(ngx_http_request_t* r, const STR_MAP &query_map);

static ngx_int_t plugin_start_subrequest(ngx_http_request_t *r);
static ngx_int_t plugin_create_ctx(ngx_http_request_t *r);
static void plugin_destroy_ctx(ngx_http_request_t *r);
static void plugin_post_body(ngx_http_request_t *r);
static ngx_int_t plugin_subrequest_post_handler(ngx_http_request_t *r, void *data, ngx_int_t rc);

static void ReplaceAll(std::string &s, const std::string &t, const std::string &w);

/*------------------------------ handler api ---------------------------------*/
void *plugin_create_handler(void *config_file, size_t len) {
    Handler *request_handler = new Handler();
    if(request_handler == NULL) {
        return NULL;
    }

    string conf_file = string((char *)config_file, len);
    request_handler->Init(conf_file);

    return request_handler;
}


ngx_int_t plugin_init_handler(void *request_handler) {
    if(request_handler == NULL) {
        return NGX_ERROR;
    }

    int rc = ((Handler *)request_handler)->InitProcess();
    if(rc != PLUGIN_OK)
        return NGX_ERROR;

    return NGX_OK;
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
 *      NGX_OK      GET request;
 *                  POST request with body read completely. 
 *      NGX_AGAIN   POST request and body read incompletely.
 *      NGX_ERROR   plugin create context error;
 *                  POST request read client request body error.
 */
ngx_int_t plugin_init_request(ngx_http_request_t *r) {
    ngx_int_t rc;

    /* create context for each http request exactly once */
    rc = plugin_create_ctx(r);
    if(rc != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                "[adfront] plugin create context error");

        return NGX_ERROR;
    }

    if(r->method & (NGX_HTTP_GET | NGX_HTTP_HEAD)) {
        return NGX_OK;
    } 

    if(r->method & NGX_HTTP_POST) {
        return ngx_http_read_client_request_body(r, plugin_post_body);
    }

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
            "[adfront] only GET/HEAD/POST request accepted");

    return NGX_ERROR;
}

/*
 * @return 
 *      NGX_OK      plugin process request sucess   
 *      NGX_AGAIN   plugin has subrequest to be processed
 *      NGX_ERROR   plugin process reuquest fail
 */
ngx_int_t plugin_process_request(void *request_handler, ngx_http_request_t *r) {
    ngx_int_t rc;
    ngx_http_adfront_ctx_t *ctx;

    ctx = (ngx_http_adfront_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    PluginContext *plugin_ctx = (PluginContext *)ctx->plugin_ctx;

    rc = ((Handler *)request_handler)->Handle(*plugin_ctx);

    if(rc == PLUGIN_AGAIN) {
        if(plugin_ctx->subrequest_uri_.empty()) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "[adfront] plugin return again without subrequest uri");
            
            return NGX_ERROR;
        }

        rc = plugin_start_subrequest(r);
        if(rc != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "[adfront] plugin start subrequest error");
            
            return NGX_ERROR;
        }

        return NGX_AGAIN;
    }

    /* assert(rc = PLUGIN_OK/PLUGIN_ERROR) */
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

    ctx = (ngx_http_adfront_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    PluginContext *plugin_ctx = (PluginContext *)ctx->plugin_ctx;

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
            "[adfront] plugin check subrequest, count = %d", r->main->count);

    st = (subrequest_t *)ctx->subrequests->elts; 
    n = ctx->subrequests->nelts;
    for(size_t i = 0; i < n; i++, st++) {
        if(st->subr->done != 1) {
            return NGX_AGAIN;
        } 
    }

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
            "[adfront] all subrequest done");

    /* clear previous subrequests result */
    plugin_ctx->subrequest_res_.clear();

    st = (subrequest_t *)ctx->subrequests->elts; 
    for(size_t i = 0; i < n; i++, st++) {
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

    ctx = (ngx_http_adfront_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    PluginContext *plugin_ctx = (PluginContext *)ctx->plugin_ctx;

    rc = ((Handler *)request_handler)->PostSubHandle(*plugin_ctx);

    if(rc == PLUGIN_AGAIN) {
        if(plugin_ctx->subrequest_uri_.empty()) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "[adfront] plugin return again without subrequest uri");
            
            return NGX_ERROR;
        }
        
        rc = plugin_start_subrequest(r);
        if(rc != NGX_OK) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "[adfront] plugin start subrequest error");
            
            return NGX_ERROR;
        }

        return NGX_AGAIN;
    } 

    /* assert(rc = PLUGIN_OK/PLUGIN_ERROR) */
    return NGX_OK;
}


/*
 * @return 
 *      NGX_OK          output filter body complete
 *      NGX_AGAIN       output filter body incomplete
 *      NGX_ERROR       plugin finalize request fail
 */
ngx_int_t plugin_final_request(ngx_http_request_t *r) {
    ngx_int_t               rc;
    ngx_buf_t               *b;
    ngx_chain_t             out;
    ngx_http_adfront_ctx_t  *ctx;

    ctx = (ngx_http_adfront_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    PluginContext *plugin_ctx = (PluginContext *)ctx->plugin_ctx;
    
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

    /* destroy request context exactly once */
    plugin_destroy_ctx(r);
    return ngx_http_output_filter(r, &out);
}

ngx_int_t plugin_done_request(ngx_http_request_t *r) {
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
            "[adfront] done request, count = %d", r->main->count);

    return ngx_http_output_filter(r, NULL);
}

void plugin_destroy_request(ngx_http_request_t *r) {
    plugin_destroy_ctx(r); 
}

/*---------------------------- local function --------------------------------*/
static ngx_int_t plugin_start_subrequest(ngx_http_request_t *r) {
    size_t n;
    subrequest_t *st;
    ngx_http_adfront_ctx_t *ctx;
    ngx_http_post_subrequest_t *psr;

    ctx = (ngx_http_adfront_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    PluginContext *plugin_ctx = (PluginContext *)ctx->plugin_ctx;

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
        string uri, args;

        if(pos != string::npos) {
            uri = url.substr(0, pos);
            args = url.substr(pos + 1);
        } else {
            uri = url.substr(0, pos);
            args = "";
        }
        
        st = (subrequest_t *)ngx_array_push(ctx->subrequests); 
        if(st == NULL) {
            return NGX_ERROR;
        }

        st->uri.data = (u_char *)ngx_pcalloc(r->pool, uri.length());
        if(st->uri.data == NULL) {
            return NGX_ERROR;
        }
        ngx_memcpy(st->uri.data, uri.c_str(), uri.length()); 
        st->uri.len = uri.length();

        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
                "[adfront] plugin subrequest args %s", uri.c_str());

        st->args.data = (u_char *)ngx_pcalloc(r->pool, args.length());
        if(st->args.data == NULL) {
            return NGX_ERROR;
        }
        ngx_memcpy(st->args.data, args.c_str(), args.length()); 
        st->args.len = args.length();

        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
                "[adfront] plugin subrequest args %s", args.c_str());
    }

    n = ctx->subrequests->nelts;
    st = (subrequest_t *)ctx->subrequests->elts;
    for(size_t i = 0; i < n; i++, st++) {
        int flags = NGX_HTTP_SUBREQUEST_IN_MEMORY | NGX_HTTP_SUBREQUEST_WAITED;

        psr = (ngx_http_post_subrequest_t *)ngx_palloc(r->pool, sizeof(ngx_http_post_subrequest_t));
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

    return NGX_OK;
}


static ngx_int_t
plugin_subrequest_post_handler(ngx_http_request_t *r, void *data, ngx_int_t rc) {
    (void)data;
    (void)rc;

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
            "[adfront] subrequest finish %V?%V", &r->uri, &r->args);

    r->parent->write_event_handler = ngx_http_core_run_phases;

    return NGX_OK;
}


static ngx_int_t plugin_create_ctx(ngx_http_request_t *r) {
    ngx_int_t rc;
    ngx_http_adfront_ctx_t *ctx;

    PluginContext *plugin_ctx = new PluginContext();
    if(plugin_ctx == NULL) {
        return NGX_ERROR;
    }

    rc = ngx_header_handler(r, plugin_ctx->headers_in_);
    if(rc != NGX_OK) {
        return NGX_ERROR;
    }

    rc = ngx_do_get_post_body(r, plugin_ctx->headers_in_); 
    if(rc != NGX_OK) {
        return NGX_ERROR;
    }
    
    ngx_query_map_print(r, plugin_ctx->headers_in_);

    ctx = (ngx_http_adfront_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    ctx->plugin_ctx = plugin_ctx;
   
    return NGX_OK;
}


static void plugin_destroy_ctx(ngx_http_request_t *r) {
    ngx_http_adfront_ctx_t *ctx;

    ctx = (ngx_http_adfront_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_adfront_module);

    if(ctx->plugin_ctx)
        delete (PluginContext *)ctx->plugin_ctx;
}


static void plugin_post_body(ngx_http_request_t *r) {
    ngx_http_adfront_ctx_t *ctx;

    ctx = (ngx_http_adfront_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    if(ctx->state == ADFRONT_STATE_INIT) {
        ngx_http_finalize_request(r, NGX_DONE);

        return;
    }

    ctx->state = ADFRONT_STATE_PROCESS;

    ngx_http_finalize_request(r, r->content_handler(r));
    ngx_http_run_posted_requests(r->connection);
}


/*-------------------------------- header process ----------------------------*/
static string ngx_http_get_cookie(ngx_http_request_t *r) {
    ngx_uint_t i, n;
    ngx_table_elt_t *cookies;

    n = r->headers_in.cookies.nelts;
    cookies = (ngx_table_elt_t*)r->headers_in.cookies.elts;

    ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0, "Cookie count: %d", n);

    for (i = 0; i < n; i++, cookies++) { 
        ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0,
            "Cookie line %d: %V", i, &cookies->value);
    }

    if(n > 0) {
        cookies = (ngx_table_elt_t*)r->headers_in.cookies.elts;
        return string((char *)cookies->value.data, cookies->value.len);
    }

    return "";
}


static string ngx_http_get_forward(ngx_http_request_t* r) {
    ngx_uint_t i, n;
    ngx_table_elt_t *tb;

    n = r->headers_in.x_forwarded_for.nelts;
    tb = (ngx_table_elt_t*)r->headers_in.x_forwarded_for.elts;

    ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0, 
        "X-Forwarded-For count: %d", n);

    for (i = 0; i < n; i++, tb++) { 
        ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0,
            "X-Forwarded-For line %d: %V", i, &tb->value);
    }
    
    if(n > 0) {
        tb = (ngx_table_elt_t *)r->headers_in.x_forwarded_for.elts;
        return string((char *)tb->value.data, tb->value.len);     
    }

    return "";
}


static string ngx_http_get_realip(ngx_http_request_t* r) {
    ngx_table_elt_t *tb;

    if(r->headers_in.x_real_ip != NULL) {
        return string((char *)r->headers_in.x_real_ip->value.data,
                r->headers_in.x_real_ip->value.len);  
    }

    if(r->headers_in.x_forwarded_for.nelts > 0) {
        tb = (ngx_table_elt_t *)r->headers_in.x_forwarded_for.elts;

        string x_forwarded_for = string((char *)tb->value.data, tb->value.len);

        size_t pos = x_forwarded_for.find(',');
        if(pos != string::npos) {
            return x_forwarded_for.substr(0, pos);
        } else {
            return x_forwarded_for;
        }
    }

    return string((char *)r->connection->addr_text.data, 
            r->connection->addr_text.len);
}


static string ngx_http_get_referer(ngx_http_request_t* r) {
    if(NULL == r->headers_in.referer){
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


static int ngx_do_get_post_body(ngx_http_request_t *r, STR_MAP& query_map) {
    if (!(r->method & (NGX_HTTP_POST))) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                "[adfront] http get request accepted");

        return NGX_OK;
    }

    
    /* NGX_HTTP_POST */
    //TODO
    query_map[HTTP_REQUEST_BODY] = "";
    
    return NGX_OK;
}


static int ngx_url_parser(const string &url, STR_MAP &kv) {
    const char *url_cstr = url.c_str();
    char *beg = (char *)url_cstr, *end = beg + url.length();
    
    while(beg < end) {
        char *delimiter = beg, *equal = beg;

        while(delimiter < end && *delimiter != '&') delimiter++;
        while(equal < delimiter && *equal != '=') equal++;
        
        if(equal == beg) {          /* key can't be empty */
            beg = delimiter + 1;
            continue;
        }
        string key = string(beg, equal - beg);

        string val;
        if((delimiter - equal - 1) > 0) {
            val = string(equal + 1, delimiter - equal - 1);
        } else {
            val = string("");
        }

        kv.insert(make_pair(key, val));

        beg = delimiter + 1;
    }

    return 0;
} 

static int ngx_header_handler(ngx_http_request_t* r, STR_MAP &query_map) {
    string tmp_str;
    string raw_str = string((char*)r->args.data, r->args.len);

    tmp_str = UriDecode(raw_str);
    query_map[HTTP_REQUEST_URL] = tmp_str;

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
            "[adfront] decode uri %s", tmp_str.c_str());

#if __URL_COMPAT__  
    /* remove "?&platform" from uri for historical reason */
    string delstr = "?&platform";
    size_t pos_url = tmp_str.find(delstr);

    if (pos_url != string::npos ) {
        tmp_str = tmp_str.substr(0, pos_url); 
    }   
#endif

    ngx_url_parser(tmp_str, query_map);

    /* plugin name */
    tmp_str = string((char*)r->unparsed_uri.data, r->unparsed_uri.len);

    size_t pos = tmp_str.find_first_of('?');
    string tmp_uri = tmp_str.substr(0, pos);

    pos = tmp_uri.find_last_of('/');
    if(pos == string::npos) {
        ngx_log_error(NGX_LOG_ERR,  r->connection->log, 0,
                "[adfront] invalid uri %s", tmp_str.c_str());

        return NGX_ERROR;
    }

    tmp_str = tmp_uri.substr(pos + 1);
    query_map[HTTP_REQUEST_PLUGINNAME] = tmp_str;

    /* cookie */
    tmp_str = ngx_http_get_cookie(r); 
    ReplaceAll(tmp_str,"\t"," ");
    query_map[HTTP_REQUEST_COOKIE] = tmp_str;

    /* method */
    if (r->method & (NGX_HTTP_POST)) {
        query_map[HTTP_REQUEST_METHOD] = HTTP_REQUEST_POST_METHOD; 
    }
    else if(r->method & (NGX_HTTP_GET)){
        query_map[HTTP_REQUEST_METHOD] = HTTP_REQUEST_GET_METHOD;
    }

    /* x-forward-for */
    tmp_str = ngx_http_get_forward(r); 
    ReplaceAll(tmp_str, "\t", " ");
    query_map[HTTP_REQUEST_HEADER_FORWARD]= tmp_str;

    /* user-agent */
    tmp_str = ngx_http_get_user_agent(r); 
    ReplaceAll(tmp_str, "\t", " ");
    query_map[HTTP_REQUEST_HEADER_USER_AGENT]= tmp_str;

    /* referer */
    tmp_str = ngx_http_get_referer(r); 
    ReplaceAll(tmp_str, "\t", " ");
    query_map[HTTP_REQUEST_HEADER_REFERER] = tmp_str;

    /* x-real-ip */
    tmp_str = ngx_http_get_realip(r); 
    ReplaceAll(tmp_str, "\t", " ");
    query_map[HTTP_REQUEST_IP] = tmp_str;

    ngx_log_error(NGX_LOG_DEBUG,  r->connection->log, 0, 
            "------------userip : %s\n", query_map[HTTP_REQUEST_IP].c_str());

    return NGX_OK;
}


static void ngx_query_map_print(ngx_http_request_t* r, const STR_MAP &query_map) {
    STR_MAP::const_iterator iter = query_map.begin();

    for(; iter != query_map.end(); iter++) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
                "[adfront] key = %s, value = %s", 
                iter->first.c_str(), iter->second.c_str());
    }
}


static void ReplaceAll(std::string &s, const std::string &t, const std::string &w) {  
    std::string::size_type pos = s.find(t), t_size = t.size(), r_size = w.size();  

    while(pos != std::string::npos){ 
	s.replace(pos, t_size, w);   
	pos = s.find(t, pos + r_size);   
    }  
}
