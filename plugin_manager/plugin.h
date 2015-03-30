#ifndef SHARELIB_PLUGIN_INTERFACE_H_
#define SHARELIB_PLUGIN_INTERFACE_H_

#include <string>
#include <vector>
#include <map>
#include <memory>


namespace sharelib{

typedef std::map<std::string, std::string> STR_MAP;


/* Derive this class to define your own handle context */
class HandleBaseCtx {
    public:
        virtual ~HandleBaseCtx() {}   
};

/* Upstream request */
struct UpstreamRequest {
    UpstreamRequest(const std::string& uri, const std::string& args)
        : uri_(uri), args_(args) {}

    int status_;                /* http status code */
    time_t up_sec_;
    time_t up_msec_;            

    std::string uri_;
    std::string args_;
    std::string response_;
};

/* If a http request has subrequests, it will be dispatched for multiple times, 
 * so we need a context to keep its infomation at run-time.
 */
struct PluginContext {
    /* Since there is no good way to predefine common interface for all 
     * dynamic library, you may need a 
     *      HandleCtx* ctx = dynmaic_cast<HandleCtx*>(handle_ctx.get()); 
     * to get your own handle context.
     */
    std::auto_ptr<HandleBaseCtx> handle_ctx_;       /* user defined context */
     
    STR_MAP headers_in_;
    STR_MAP headers_out_;

    std::vector<UpstreamRequest> upstream_request_;                   

    std::string handle_result_;     /* hanle's final result as http response */

    std::string time_stamp_;        /* time stamp for log */
};


class Plugin {
    public:
        virtual ~Plugin() {}
    public:
        virtual int Init(const STR_MAP& config_map) = 0;

        virtual int Destroy() = 0;

        /*
         * PLUGIN_OK        Plugin process request success.
         * PLUGIN_ERROR     Plugin process request fail.
         * PLUGIN_AGAIN     Requesst isn't finished, there are subrequests to be processed.
         */
        virtual int Handle(PluginContext &ctx) = 0;

        virtual int PostSubHandle(PluginContext &ctx) = 0;
};


}
#endif //end SHARELIB_PLUGIN_INTERFACE_H_
