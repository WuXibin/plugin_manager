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


/* Store plugin centext for every http request */
class PluginContext {
    public:
        std::auto_ptr<HandleBaseCtx> handle_ctx_; 
         
        STR_MAP headers_in_;
        STR_MAP headers_out_;

        std::vector<std::string> subrequest_uri_;                   
        std::vector<std::string> subrequest_res_;

        std::string handle_result_;
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
