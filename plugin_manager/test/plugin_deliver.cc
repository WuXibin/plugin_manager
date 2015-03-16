#include <assert.h>
#include <iplugin.h>
#include <plugin_config.h>

#include <iostream>

using namespace std;
using namespace sharelib;


class DeliverCtx: public HandleBaseCtx {
    public:
        int state;

        DeliverCtx() { cout << "DeliverCtx()" << endl; }
        ~DeliverCtx() { cout << "~DeliverCtx()" << endl; }
    private:
};


class PluginDeliver: public IPlugin {
    public:
        int Init(const STR_MAP& config_map);

        int Destroy();

        int Handle(IPluginCtx *ctx);

        int PostSubHandle(IPluginCtx *ctx);

    private:
        //add plugin share infomation here
};

int PluginDeliver::Init(const STR_MAP& config_map) {
    STR_MAP::const_iterator iter = config_map.find(PLUGIN_CONF);
    
    if(iter != config_map.end()) {
        cout << PLUGIN_CONF << " : " << iter->second << endl;
        
        return PLUGIN_OK;
    }

    return PLUGIN_ERROR;
}

int PluginDeliver::Destroy() {
    cout << "PluginDeliver::Destroy()" << endl;
    
    return PLUGIN_OK;
}

int PluginDeliver::Handle(IPluginCtx *ctx) {
    DeliverCtx *deliver_ctx = new DeliverCtx();

    ctx->handle_ctx_.reset(deliver_ctx);
    deliver_ctx->state = 0;

    STR_MAP::const_iterator citer = ctx->headers_in_.find(HTTP_REQUEST_URL);
    
    if(citer == ctx->headers_in_.end()) {
        cout << HTTP_REQUEST_URL << "no found" << endl;
        
        return PLUGIN_ERROR;
    } else {
        cout << HTTP_REQUEST_URL << " : " << citer->second << endl;
    }

    
    string old_api = "/oldhandler?a=2&pm=1&v=4.4.5&areaid=21233&pageid=1&platform=2&city=110100&series=&devicebrand=apple&devicemodel=iPhone&networkid=0&idfa=6A6F7AD2-D56E-4E85-81D5-003CEF9E7F2A&deviceid=438e54444b643a66ab4d16cf852386adc32d2ec5&mac=0&ip=127.0.0.1";

    string new_api = "/newhandler?a=2&pm=1&v=4.4.5&areaid=21233&pageid=1&platform=2&city=110100&series=&devicebrand=apple&devicemodel=iPhone&networkid=0&idfa=6A6F7AD2-D56E-4E85-81D5-003CEF9E7F2A&deviceid=438e54444b643a66ab4d16cf852386adc32d2ec5&mac=0&ip=127.0.0.1";

    ctx->subrequest_uri_.push_back(old_api);
    ctx->subrequest_uri_.push_back(new_api);

    return PLUGIN_AGAIN;
}


int PluginDeliver::PostSubHandle(IPluginCtx *ctx) { 
    DeliverCtx *deliver_ctx = dynamic_cast<DeliverCtx *>(ctx->handle_ctx_.get());

    for(size_t i = 0; i < ctx->subrequest_res_.size(); i++) {
        cout << ctx->subrequest_res_[i] << endl;
    }
    
    if(deliver_ctx->state == 0) {
        /* clear previous subrequests */
        ctx->subrequest_uri_.clear();

        string new_api = "/newhandler?a=2&pm=1&v=4.4.5&areaid=21233&pageid=1&platform=2&city=110100&series=&devicebrand=apple&devicemodel=iPhone&networkid=0&idfa=6A6F7AD2-D56E-4E85-81D5-003CEF9E7F2A&deviceid=438e54444b643a66ab4d16cf852386adc32d2ec5&mac=0&ip=127.0.0.1";

        ctx->subrequest_uri_.push_back(new_api);
        deliver_ctx->state = 1;

        return PLUGIN_AGAIN;
    } 
        
    assert(deliver_ctx->state == 1); 
    ctx->handle_result_.append(ctx->subrequest_res_[0]);

    return PLUGIN_OK;
}


extern "C" {
    IPlugin* create_instance() {
        return new (std::nothrow)PluginDeliver;
    }   
}
