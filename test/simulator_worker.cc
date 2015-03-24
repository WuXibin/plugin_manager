#include <iostream>
#include <string>

#include "engine_adfront.pb.h"
#include "../plugin_manager/plugin.h"
#include "../plugin_manager/plugin_config.h"

using namespace std;
using namespace sharelib;
using namespace AdEngineFront;

int mock_request(std::string& request_data);
void mock_pageinfo_pb(AdFrontRequest& adfront_request);
void mock_mobile_pb(AdFrontRequest& adfront_request);
void mock_position_pb(AdFrontRequest& adfront_request);
int parse_response(const std::string res_message);


class AdserverTest: Plugin {
    public:
        int Init(const STR_MAP& config_map); 
        
        int Destroy();

        int Handle(PluginContext& ctx);
        
        int PostSubHandle(PluginContext& ctx);
};

int AdserverTest::Init(const STR_MAP& config_map) {
    (void)config_map;

    cout << "AdserverTest::Init()" << endl;

    return 0;
}

int AdserverTest::Destroy() {
    return 0;
}

int AdserverTest::Handle(PluginContext& ctx) {
    string request_data;
    
    mock_request(request_data);    

    cout << "AdserverTest::Handle()" << endl;
    ctx.upstream_request_.push_back(UpstreamRequest("/adserver", request_data));

    return PLUGIN_AGAIN;
}

int AdserverTest::PostSubHandle(PluginContext& ctx) {
    const UpstreamRequest& ups_request = ctx.upstream_request_[0]; 

    ctx.handle_result_ = "Congratulation! It finally works!";
    cout << ups_request.up_sec_ << "s." << ups_request.up_msec_ << "ms" << endl;
    if(ups_request.status_ != 200) {
        cout << "upstream error " << ups_request.status_ << endl;

        return PLUGIN_OK;
    }

    parse_response(ups_request.response_);

    return PLUGIN_OK;
}

int mock_request(std::string& request_data) {
    AdFrontRequest adfront_request;
    adfront_request.set_req_id("1");
    adfront_request.set_ip("27.184.95.255");
    adfront_request.set_user_agent("IE");
    adfront_request.set_user_id("0001B1E0-BA7F-640F-EBAE-197510B08DA5");

    mock_pageinfo_pb(adfront_request);
    mock_mobile_pb(adfront_request);
    mock_position_pb(adfront_request);

    adfront_request.SerializePartialToString(&request_data);

    return 0;
}

void mock_pageinfo_pb(AdFrontRequest& adfront_request) {
    AdFrontRequest_PageInfo* request_pageinfo;
    request_pageinfo = adfront_request.mutable_page_info();
    char CCH[] = "_0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
    char ch[32 + 1] = {0};

    for (int i = 0; i < 32; ++i) {
        int x = rand() % (sizeof(CCH) - 1);

        ch[i] = CCH[x];
    }

    std::string pageid = std::string(ch, 32);
    request_pageinfo->set_page_id(pageid);
    request_pageinfo->set_is_multiple(false);
    request_pageinfo->set_page_province("110000");
    request_pageinfo->set_page_city("110000");
    request_pageinfo->add_page_auto_brand("16");
    request_pageinfo->add_page_auto_brand("benchi");
    request_pageinfo->add_page_auto_serie("001");
    request_pageinfo->add_page_auto_serie("4x");
    request_pageinfo->add_page_auto_level("big");
    request_pageinfo->add_page_auto_level("4");
}

void mock_mobile_pb(AdFrontRequest& adfront_request) {
    AdFrontRequest_Mobile* request_mobile;
    request_mobile = adfront_request.mutable_mobile();

    request_mobile->set_platform("iphone");
    request_mobile->set_brand("Apple");
    request_mobile->set_model("iphone");
    request_mobile->set_os_version("ios7.4.1");
    request_mobile->set_connection_type("WIFI");
    request_mobile->set_is_app(false);
    request_mobile->set_latitude(39);
    request_mobile->set_longitude(116);
}

void mock_position_pb(AdFrontRequest& adfront_request) {
    AdFrontRequest_PositionInfo* request_position = NULL;

    request_position = adfront_request.add_position_info();
    request_position->set_position_id(29);
    request_position->set_pv_id("10009-pvid");

}

int parse_response(const std::string res_message) {
    AdFrontResponse adfront_response;
    std::string log_msg;

    if(!adfront_response.ParseFromString(res_message)) {
        cout << "protobuf parse error" << endl;
        return -1;
    }

    cout << adfront_response.req_id() << endl;
    for(int i = 0; i < adfront_response.position_info_size(); ++i) {
        const AdFrontResponse::PositionInfo& pos_info = adfront_response.position_info(i); 

        cout << pos_info.position_id() << endl; 
        
        if(pos_info.has_pv_id()) {
            cout << pos_info.pv_id() << endl;
        }
    }

    return 0;
}

extern "C" {

AdserverTest* create_instance() {
    return new (std::nothrow)AdserverTest;
}

};
