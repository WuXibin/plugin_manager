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
        HandleBaseCtx *handle_ctx() { return handle_ctx_.get(); }
        void set_handle_ctx(HandleBaseCtx *ctx) { handle_ctx_.reset(ctx); }
        
        const STR_MAP& headers_in() const { return headers_in_; }
        const STR_MAP& headers_out() const { return headers_out_; }
        
        const std::vector<std::string>& subrequest_uri() const { return subrequest_uri_; }
        const std::vector<std::string>& subrequest_res() const { return subrequest_res_; }

        void AddSubrequest(const std::string& uri) { 
            subrequest_uri_.push_back(uri); 
        }

        void AddHeadersIn(const std::string& key, const std::string& val) { 
            headers_in_[key] = val;
        }

        void AddHeadersOut(const std::string& key, const std::string& val) { 
            headers_out_[key] = val;
        }
        
        const std::string& handle_result() const { return handle_result_; }
        void set_handle_result(const std::string& result) {
            handle_result_ = result;
        }

    private:
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

        virtual int Handle(IPluginCtx *ctx) = 0;

        virtual int PostSubrequestHandle(IPluginCtx *ctx) { 
            (void)ctx;      //avoid compiler complain
            return 0; 
        }
};


}
#endif //end SHARELIB_PLUGIN_INTERFACE_H_
