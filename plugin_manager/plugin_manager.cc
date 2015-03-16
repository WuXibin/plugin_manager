#include <dlfcn.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <utility>
#include <google/protobuf/text_format.h>

#include "plugin_manager.h"

using namespace std;

namespace sharelib {

const static string kCreatePluginFunc = "create_instance";
const static string kStringSeparator = "=";

typedef Plugin *(*CreatePluginFunc)();


PluginManager::PluginManager() {
}


PluginManager::~PluginManager() {
}


int PluginManager::Init(const string& config_file) {
    string config_str;

    if(config_file.empty()) {
        cerr << "plugin_manager config_file empty" << endl;
        return -1;
    }

    if(ReadFileContent(config_file, config_str) != 0) {
        cerr << "plugin_manager open " << config_file << " error" << endl;
        return -1;
    }

    bool rc = google::protobuf::TextFormat::ParseFromString(config_str, &config_obj_);
    if (!rc) {
        cerr << "plugin_manager parse " << config_file << " error" << endl;
        return -1;
    }

    cout << "plugin_manager read " << config_file << " success" << endl;

    plugin_mananger_conf_ = config_file;

    return LoadPlugins();
}


int PluginManager::LoadPlugin(PluginInfoPtr &plugin_info) {  
    string so_path = plugin_info->plugin_conf.so_home_path()
        + '/' + plugin_info->plugin_conf.so_name();

    void *so_handler = dlopen(so_path.c_str(), RTLD_LAZY);
    if (so_handler == NULL) {
        cerr << "plugin_manager dlopen path=" << so_path 
            << ", error=" << dlerror() << endl; 

        return -1;
    }

    CreatePluginFunc handler = NULL;
    handler = (CreatePluginFunc)dlsym(so_handler, kCreatePluginFunc.c_str());
    if (handler == NULL) {
        dlclose(so_handler);
        cerr << "plugin_manager dlsym path=" << so_path
            << ", error=" << dlerror() << endl;

        return -1;
    }

    Plugin* plugin = (*handler)();

    if (plugin == NULL) {
        dlclose(so_handler);
        cerr << "plugin_manager create_instance path=" << so_path << endl;

        return -1;
    }

    int rc = plugin->Init(plugin_info->conf_map);
    if(rc != 0) {
        cerr << "plugin_manager plugin init error" << so_path << endl;
        
        return -1;
    }

    plugin_info->plugin_ptr = plugin;
    plugin_info->so_handler = so_handler;
    return 0;
}


int PluginManager::LoadPlugins() {
    cout << "plugin_manager load plugins " << PLUGIN_MANAGER_CONF 
            << " : " << plugin_mananger_conf_ << endl;

    for (int i = 0; i < config_obj_.plugin_conf_list_size(); ++i) {
        PluginInfoPtr plugin_info_ptr(new PluginInfo());

        plugin_info_ptr->plugin_conf = config_obj_.plugin_conf_list(i);
        plugin_info_ptr->conf_map[PLUGIN_CONF] = plugin_info_ptr->plugin_conf.conf_path();
        plugin_info_ptr->conf_map[PLUGIN_MANAGER_CONF] = plugin_mananger_conf_;

        cout << "plugin_manager plugin " << PLUGIN_CONF  
            << " : " << plugin_info_ptr->conf_map[PLUGIN_CONF] << endl;

        for (int j = 0; j < plugin_info_ptr->plugin_conf.key_val_list_size(); ++j) {
            string key_val = plugin_info_ptr->plugin_conf.key_val_list(j);
            ParseStr2Map(key_val, plugin_info_ptr->conf_map);
        }

        int rc = LoadPlugin(plugin_info_ptr);
        if (rc != 0) {
            cout << "plugin_manager load plugin error" << endl;

            return -1;
        }

        for (int j = 0; j < plugin_info_ptr->plugin_conf.name_size(); ++j){
            plugins_info_map_.insert(make_pair<string, PluginInfoPtr>(
                        plugin_info_ptr->plugin_conf.name(j), 
                        plugin_info_ptr));
        }
    }
    return 0;
}

Plugin *PluginManager::GetPlugin(const string &queryName) {
    PluginInfoPtrMap::iterator it = plugins_info_map_.find(queryName);
    if (it == plugins_info_map_.end()) {
        return NULL;
    }

    return it->second->plugin_ptr;
}


int PluginManager::ReadFileContent(const string& config_file, string &content) {
    ifstream fin(config_file.c_str());
    if (!fin.is_open()) { 
        return -1;
    }

    ostringstream tmp;
    tmp << fin.rdbuf();
    content = tmp.str();
    fin.close();

    return 0;
}


// Parse key=value in config_file to map
int PluginManager::ParseStr2Map(const std::string& content, STR_MAP& content_map) {
    if (content.size() == 0) {
        return 0;
    }

    string key, value;
    size_t pos = content.find(kStringSeparator);
    if (pos != string::npos) {
        key = content.substr(0, pos);
        value = content.substr(pos+1);
        content_map[key] = value;
    }

    return 0;
}


}

