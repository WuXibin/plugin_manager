#include "ngx_handler.h"
#include "ngx_handler_config.h"

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


int Handler::Init(const STR_MAP& config_map) {
    config_map_ = config_map;

    return PLUGIN_OK;
}


// Init process,every worker process use this function to complete init.
int Handler::InitProcess() {
    plugin_manager_ = new PluginManager();
    if (plugin_manager_ == NULL) {
        return PLUGIN_ERROR;
    }

    STR_MAP::const_iterator iter = config_map_.find(PLUGIN_MANAGER_HOME_PATH);
    if (iter != config_map_.end()) {
        cout << "key:" << PLUGIN_MANAGER_HOME_PATH 
            << " value:" << iter->second.c_str() << endl;

        return plugin_manager_->Init(iter->second.c_str()); 
    } 
    
    cout << PLUGIN_MANAGER_HOME_PATH << " not exist" << endl;
    return PLUGIN_ERROR;
}


void Handler::Destroy() {

}

int Handler::Handle(const STR_MAP& query_map,STR_MAP& kv_out, string& result) {
    STR_MAP::const_iterator iter = query_map.find(HTTP_REQUEST_PLUGINNAME);

    if (iter != query_map.end()) {
        IPlugin* plugin = static_cast<IPlugin *>(plugin_manager_->GetPlugin(iter->second));
        if (plugin == NULL) {
            return PLUGIN_ERROR;
        }

        int rc = plugin->Handle(query_map, kv_out, result);
        

        if (RET_OK != ret) {
            return ret;
        }
    }
    return RET_OK;
}

}
