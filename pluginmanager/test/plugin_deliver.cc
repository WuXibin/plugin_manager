#include <iplugin.h>
#include <plugin_config.h>

#include <iostream>

using namespace std;
using namespace sharelib;


class DeliverCtx: public HandleBaseCtx {
    public:
        DeliverCtx() { cout << "DeliverCtx()" << endl; }
        ~DeliverCtx() { cout << "~DeliverCtx()" << endl; }
    private:
};


class PluginDeliver: public IPlugin {
    public:
        int Init(const STR_MAP& config_map);

        int Destroy();

        int Handle(IPluginCtx *ctx);

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

    ctx->set_handle_ctx(deliver_ctx);

    STR_MAP::const_iterator citer = ctx->headers_in().find(HTTP_REQUEST_URL);
    
    if(citer == ctx->headers_in().end()) {
        cout << HTTP_REQUEST_URL << "no found" << endl;
        
        return PLUGIN_ERROR;
    } else {
        cout << HTTP_REQUEST_URL << " : " << citer->second << endl;
    }

    ctx->set_handle_result("hello world");

    return PLUGIN_OK;
}

extern "C" {
    IPlugin* create_instance() {
        return new (std::nothrow)PluginDeliver;
    }   
}
