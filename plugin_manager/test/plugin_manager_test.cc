#include <iplugin.h>
#include <plugin_config.h>
#include <plugin_manager.h>

#include <iostream>

using namespace std;
using namespace sharelib;

int main() {
    PluginManager  plugin_manager;

    plugin_manager.Init("plugin_manager.conf"); 

    //new request comming ...
    IPlugin *plugin = plugin_manager.GetPlugin("deliver");
    
    IPluginCtx *ctx = new IPluginCtx(); 

    //ctx->AddHeadersIn(HTTP_REQUEST_URL, "/deliver?hello world");

    plugin->Handle(ctx);


    cout << "result: " << ctx->handle_result_ << endl;

    delete ctx;

    return 0;
}
