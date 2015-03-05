#ifndef SHARELIB_PLUGINMANANGER_PLUGIN_MANAGER_H_
#define SHARELIB_PLUGINMANANGER_PLUGIN_MANAGER_H_

#include "iplugin.h"
#include "plugin_config.h"
#include "plugin_manager.conf.pb.h"

#include <dlfcn.h>
#include <map>
#include <string>
#include <tr1/memory>

namespace sharelib {

struct PluginInfo;
class PluginManager;

typedef std::tr1::shared_ptr<PluginInfo> PluginInfoPtr;
typedef std::map<std::string, PluginInfoPtr> PluginInfoPtrMap;

struct PluginInfo {
    PluginConf  plugin_conf;
    IPlugin*    plugin_ptr;
    STR_MAP     conf_map;
    void*       so_handler;

    PluginInfo() {
        so_handler = NULL;
        plugin_ptr = NULL;
    }

    ~PluginInfo() {
        if(plugin_ptr != NULL) {
              delete plugin_ptr;
              plugin_ptr = NULL;
        }

        if(so_handler != NULL) {
              dlclose(so_handler);
              so_handler = NULL;
        }
    }
};

class PluginManager {
public:
    PluginManager();

    virtual ~PluginManager();

    int Init(const std::string& conf_file);

    IPlugin* GetPlugin(const std::string &plugin_name);

private:
    int LoadPlugin(PluginInfoPtr& plugin_info);

    int LoadPlugins();

    int ParseStr2Map(const std::string& content, STR_MAP& content_map);

    int ReadFileContent(const std::string& config_file, std::string &content);

    const PluginInfoPtrMap& GetPlugins() const { return plugins_info_map_; }

private:
    PluginManagerConf config_obj_;

    PluginInfoPtrMap plugins_info_map_;
    std::string plugin_mananger_conf_;

};

}

#endif // end SHARELIB_PLUGINMANANGER_PLUGIN_MANAGER_H_

