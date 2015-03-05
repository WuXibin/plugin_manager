#ifndef __PLUGIN_CONFIG_H__
#define __PLUGIN_CONFIG_H__

#define PLUGIN_OK      0
#define PLUGIN_ERROR   -1
#define PLUGIN_AGAIN   -2

#define PLUGIN_MANAGER_CONF             "__plugin_manager_conf__"
#define PLUGIN_CONF                     "__plugin_conf__"

#define HTTP_REQUEST_POST_BODY          "__post_body__"
#define HTTP_REQUEST_URL                "__url__"
#define HTTP_REQUEST_COOKIE             "__cookie__"
#define HTTP_REQUEST_METHOD             "__method__"
#define HTTP_REQUEST_GET_METHOD         "__get__"
#define HTTP_REQUEST_POST_METHOD        "__post__"
#define HTTP_REQUEST_HEADER_FORWARD     "__forwarded__"
#define HTTP_REQUEST_HEADER_REFERER     "__referer__"
#define HTTP_REQUEST_HEADER_USER_AGENT  "__user_agent__"
#define HTTP_REQUEST_PLUGINNAME         "__plugin_name__"

#endif 
