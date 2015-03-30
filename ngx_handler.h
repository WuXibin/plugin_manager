#ifndef ADFRONT_HANDLER_MANAGER_HANDLER_H_
#define ADFRONT_HANDLER_MANAGER_HANDLER_H_


#include <string>
#include <vector>
#include <map>

#include <plugin_manager/plugin.h>
#include <plugin_manager/plugin_config.h>
#include <plugin_manager/plugin_manager.h>


namespace ngx_handler{

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
