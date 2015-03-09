#include "ngx_handler.h"

#include <iostream>

using namespace std;
using namespace sharelib;

namespace ngx_handler {

Handler::Handler(): plugin_manager_(NULL) {
}


Handler::~Handler() {
    if(plugin_manager_)
        delete plugin_manager_;
}

void Handler::Destroy() {
}


int Handler::Init(const string& config_file) {
    config_file_ = config_file;

    return PLUGIN_OK;

}


int Handler::InitProcess() {
    plugin_manager_ = new PluginManager();
    if (plugin_manager_ == NULL) {
        return PLUGIN_ERROR;
    }

    return plugin_manager_->Init(config_file_); 
}


int Handler::Handle(IPluginCtx *ctx) {
    STR_MAP::const_iterator iter = ctx->headers_in_.find(HTTP_REQUEST_PLUGINNAME);

    if (iter != ctx->headers_in_.end()) {
        IPlugin* plugin = static_cast<IPlugin *>(plugin_manager_->GetPlugin(iter->second));
        if (plugin == NULL) {
            return PLUGIN_ERROR;
        }

        return plugin->Handle(ctx);
    }

    return PLUGIN_ERROR;
}


int Handler::PostSubHandle(IPluginCtx *ctx) {
    STR_MAP::const_iterator iter = ctx->headers_in_.find(HTTP_REQUEST_PLUGINNAME);

    if (iter != ctx->headers_in_.end()) {
        IPlugin* plugin = static_cast<IPlugin *>(plugin_manager_->GetPlugin(iter->second));
        if (plugin == NULL) {
            return PLUGIN_ERROR;
        }

        return plugin->PostSubHandle(ctx);
    }

    return PLUGIN_ERROR;
}

}
