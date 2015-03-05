#ifndef SHARELIB_PLUGIN_INTERFACE_H_
#define SHARELIB_PLUGIN_INTERFACE_H_

#include <string>
#include <vector>
#include <map>
#include <memory>


namespace sharelib{

typedef std::map<std::string, std::string> STR_MAP;


//Derive this class to define your own handle context
class HandleBaseCtx {
    public:
        virtual ~HandleBaseCtx() {}   
};


//Store plugin centext for every http request
class IPluginCtx {
    public:
        std::auto_ptr<HandleBaseCtx> handle_ctx_; 
         
        STR_MAP headers_in_;
        STR_MAP headers_out_;

        std::vector<std::string> subrequest_uri_;                   
        std::vector<std::string> subrequest_res_;

        std::string handle_result_;
};


class IPlugin {
    public:
        virtual ~IPlugin() {}
    public:
        virtual int Init(const STR_MAP& config_map) = 0;

        virtual int Destroy() = 0;

        /*
         * PLUGIN_DONE      Process request done, this are no subrequests.
         * PLUGIN_AGAIN     There are subrequest to be processed
         */
        virtual int Handle(IPluginCtx *ctx) = 0;

        virtual int PostSubHandle(IPluginCtx *ctx) { 
            (void)ctx;      //avoid compiler complain
            return 0; 
        }
};


}
#endif //end SHARELIB_PLUGIN_INTERFACE_H_
