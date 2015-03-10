// copyright:
//            (C) SINA Inc.
//
//      file: handler.h
//      desc: declaration for Hander--the mainly work process
//    author: kefeng
//     email: xidianzkf@gmail.com
//      date: 2013-04-18
//
//    change: 

#ifndef ADFRONT_HANDLER_MANAGER_HANDLER_H_
#define ADFRONT_HANDLER_MANAGER_HANDLER_H_


#include <string>
#include <vector>
#include <map>

#include "pluginmanager/plugin.h"
#include "pluginmanager/plugin_config.h"
#include "pluginmanager/plugin_manager.h"


namespace ngx_handler{

typedef std::map<std::string, std::string> STR_MAP;

class Handler {
    public:
        Handler();
        ~Handler();

        // Init handler config;
        int Init(const std::string& config_file);

        // Init work process.
        int InitProcess();

        // release the resouces
        void Destroy();

        // handle one request
        int Handle(sharelib::PluginContext &ctx);

        int PostSubHandle(sharelib::PluginContext &ctx);

    private:
        sharelib::PluginManager* plugin_manager_;
        std::string config_file_;
};

}
#endif  // ADFRONT_HANDLER_MANAGER_HANDLER_H_
