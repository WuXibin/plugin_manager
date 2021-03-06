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

string kIsJumpUrl = "is_jumpurl";
string kJumpUrl = "u";

string kIscookie = "is_cookie";
string kCookievalue = "pvidlist";
string kCookiedomain = "cookie_domain";
string kCookiepath = "cookie_path";
string kIscookieexpires = "is_cookie_expires";
string kCookieexpires = "cookie_expires";


static string ngx_http_get_cookie(ngx_http_request_t *r);
static string ngx_http_get_forward(ngx_http_request_t* r);
static string ngx_http_get_referer(ngx_http_request_t* r);
static string ngx_http_get_user_agent(ngx_http_request_t* r);
static string ngx_http_get_realip(ngx_http_request_t* r);

static int ngx_header_handler(ngx_http_request_t* r, STR_MAP &query_map);
static int ngx_do_get_post_body(ngx_http_request_t *r, STR_MAP& query_map);
static int ngx_url_parser(const string &url, STR_MAP& kv);
static void ngx_query_map_print(ngx_http_request_t* r, const STR_MAP &query_map);
static int ngx_url_jump(ngx_http_request_t* r, const STR_MAP &kv_out);
static int ngx_write_cookie(ngx_http_request_t* r, const STR_MAP &kv_out);

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
 *      NGX_ERROR   POST request read client request body error.
 */
ngx_int_t plugin_init_request(ngx_http_request_t *r) {
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

    /* create context for each http request exactly once */
    rc = plugin_create_ctx(r);
    if(rc != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                "[adfront] plugin create context error");

        return NGX_ERROR;
    }

    ctx = (ngx_http_adfront_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_adfront_module);
    PluginContext *plugin_ctx = (PluginContext *)ctx->plugin_ctx;

    rc = ((Handler *)request_handler)->Handle(*plugin_ctx);
    if(rc == PLUGIN_NOT_FOUND) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                "[adfront] plugin not found, check plugin_manager.conf");

        return NGX_ERROR;
    }

    if(rc == PLUGIN_AGAIN) {
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
    subrequest_t            *st;
    ngx_http_upstream_t     *up;
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

    st = (subrequest_t *)ctx->subrequests->elts; 
    vector<UpstreamRequest>::iterator it = plugin_ctx->upstream_request_.begin();
    for(size_t i = 0; i < n; i++, st++, it++) {
        up = st->subr->upstream;
        if(up == NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                    "[adfront] plugin subrequest upstream null, location not found ?");

            return NGX_ERROR;
        }

        it->status_ = up->state->status;
        it->up_sec_ = up->state->response_sec;
        it->up_msec_ = up->state->response_msec;
        it->response_ = string((char *)up->buffer.pos, up->buffer.last - up->buffer.pos);  
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

    ngx_url_jump(r, plugin_ctx->headers_out_);
    ngx_write_cookie(r, plugin_ctx->headers_out_);
    
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

    /* 302 url jump */
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
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
            "[adfront] destroy request context, count = %d", r->main->count);

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

    n = plugin_ctx->upstream_request_.size();
    for(size_t i = 0; i < n; i++) {
        UpstreamRequest& ups = plugin_ctx->upstream_request_[i];
        
        st = (subrequest_t *)ngx_array_push(ctx->subrequests); 
        if(st == NULL) {
            return NGX_ERROR;
        }

        st->uri.data = (u_char *)ngx_pcalloc(r->pool, ups.uri_.length());
        if(st->uri.data == NULL) {
            return NGX_ERROR;
        }
        ngx_memcpy(st->uri.data, ups.uri_.c_str(), ups.uri_.length()); 
        st->uri.len = ups.uri_.length();

        st->args.data = (u_char *)ngx_pcalloc(r->pool, ups.args_.length());
        if(st->args.data == NULL) {
            return NGX_ERROR;
        }
        ngx_memcpy(st->args.data, ups.args_.c_str(), ups.args_.length()); 
        st->args.len = ups.args_.length();

    }

    n = ctx->subrequests->nelts;
    st = (subrequest_t *)ctx->subrequests->elts;
    for(size_t i = 0; i < n; i++, st++) {
        int flags = NGX_HTTP_SUBREQUEST_IN_MEMORY | NGX_HTTP_SUBREQUEST_WAITED;

        psr = (ngx_http_post_subrequest_t *)ngx_palloc(r->pool, 
                sizeof(ngx_http_post_subrequest_t));
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

    char buf[32];
    struct timeval tv;

    gettimeofday(&tv, NULL);
    snprintf(buf, 32, "%ld.%06ld", tv.tv_sec, tv.tv_usec);
    plugin_ctx->time_stamp_ = string(buf + 5, strlen(buf) - 5);

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

    ctx->plugin_ctx = NULL;
}


static void plugin_post_body(ngx_http_request_t *r) {
    ngx_http_adfront_ctx_t *ctx;

    ctx = (ngx_http_adfront_ctx_t *)ngx_http_get_module_ctx(r, ngx_http_adfront_module);

    /* read whole request body at first time */
    if(ctx->state == ADFRONT_STATE_INIT) {
        ngx_http_finalize_request(r, NGX_DONE);

        return;
    }

    /* read request body in multiple times */
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
    size_t      len = 0;
    ssize_t     nbytes = 0;
    u_char      *buf, *last;
    ngx_chain_t *cl;

    if (!(r->method & (NGX_HTTP_POST))) {
        ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
                "[adfront] http GET/HEAD request accepted");

        return NGX_OK;
    }

    if(r->request_body == NULL || r->request_body->bufs == NULL) {
        query_map[HTTP_REQUEST_BODY] = "";
        
        return NGX_OK; 
    } 

    for(cl = r->request_body->bufs; cl; cl = cl->next) {
        if(ngx_buf_in_memory(cl->buf)) {
            len += cl->buf->last - cl->buf->pos; 
        } else {
            len += cl->buf->file_last - cl->buf->file_pos;
        }
    } 

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
            "[adfront] post body length: %d", len);

    if(len == 0) {
        query_map[HTTP_REQUEST_BODY] = "";
        
        return NGX_OK; 
    }

    last = buf = (u_char *)ngx_palloc(r->pool, len);
    if(buf == NULL) {
        return NGX_ERROR;
    }

    for(cl = r->request_body->bufs; cl; cl = cl->next) {
        if(ngx_buf_in_memory(cl->buf)) {
            last = ngx_copy(last, cl->buf->pos, cl->buf->last - cl->buf->pos); 
        } else {
            nbytes = ngx_read_file(cl->buf->file, last, 
                    cl->buf->file_last - cl->buf->file_pos, cl->buf->file_pos);

            if(nbytes < 0) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                        "[adfront] post body read temp file error"); 

                return NGX_ERROR;
            } else {
                last += nbytes; 
            }
        }
    } 
    query_map[HTTP_REQUEST_BODY] = string((char *)buf, len);
    
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


static int ngx_url_jump(ngx_http_request_t* r, const STR_MAP &kv_out) {
    STR_MAP::const_iterator iter;
    iter = kv_out.find(kIsJumpUrl);

    if(iter == kv_out.end() || iter->second != "1") {
        return NGX_OK;
    }

    iter = kv_out.find(kJumpUrl);
    if(iter == kv_out.end()) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                "[adfront] set url jump without specified jump url");

        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
            "[adfront] write cookies: %s", iter->second.c_str());

    r->headers_out.status = NGX_HTTP_MOVED_TEMPORARILY;

    ngx_table_elt_t* h = (ngx_table_elt_t*)(ngx_list_push(&r->headers_out.headers));
    if(h == NULL) {
        return NGX_ERROR;
    }

    h->hash = 1;
    h->key.len = sizeof("Location") - 1;
    h->key.data = (u_char *)"Location";

    h->value.data = (u_char *)ngx_palloc(r->pool, iter->second.length());
    if(h->value.data == NULL) {
        return NGX_ERROR;
    }
    ngx_memcpy(h->value.data, iter->second.c_str(), iter->second.length());
    h->value.len = iter->second.size();

    return NGX_OK;
}


static int ngx_write_cookie(ngx_http_request_t* r, const STR_MAP &kv_out) {
    STR_MAP::const_iterator iter;
    iter = kv_out.find(kIscookie);

    if(iter == kv_out.end() || iter->second != "1") {
        return NGX_OK;
    }

    iter = kv_out.find(kCookievalue);
    if(iter != kv_out.end()) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, 
                "[adfront] set write cookies without cookies value");

        return NGX_ERROR;
    }

    string value = "pvidlist=" + iter->second;

    iter = kv_out.find(kCookiedomain);
    if(iter != kv_out.end()) value = value + "; domain=" + iter->second;

    iter = kv_out.find(kCookiepath);
    if(iter != kv_out.end()) value = value + "; path=" + iter->second;

    iter = kv_out.find(kIscookieexpires);
    if(iter != kv_out.end()){ 
        iter = kv_out.find(kCookieexpires);
        if(iter != kv_out.end()) value = value + "; expires=" + iter->second;
    }
    
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, 
            "[adfront] write cookies: %s", value.c_str());

    ngx_table_elt_t *cookie = (ngx_table_elt_t*)ngx_list_push(&r->headers_out.headers);
    if(cookie == NULL) {                           
        return NGX_ERROR;                               
    }                                                   

    cookie->hash = 1;
    cookie->key.len = sizeof("Set-Cookie") - 1;
    cookie->key.data = (u_char *) "Set-Cookie";

    cookie->value.data = (u_char *)ngx_palloc(r->pool, value.length()); 
    if(cookie->value.data == NULL) {
        return NGX_ERROR;
    }
    ngx_memcpy(cookie->value.data, value.c_str(), value.length());
    cookie->value.len = value.length();            

    return NGX_OK;
}


static void ReplaceAll(std::string &s, const std::string &t, const std::string &w) {  
    std::string::size_type pos = s.find(t), t_size = t.size(), r_size = w.size();  

    while(pos != std::string::npos){ 
	s.replace(pos, t_size, w);   
	pos = s.find(t, pos + r_size);   
    }  
}
