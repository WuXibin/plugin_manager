A Plugin Manager for Nginx

Prerequisite
====================================
1. cd nginx-1.6.2/module_adfront/plugin_manager
2. make

Build
====================================
1. cd nginx-1.6.2
2. ./configure --add-module=module_adfront --add-module=module_adserver --with-http_realip_module
3. make

Run
====================================
sudo sbin/nginx -p `pwd` -c conf/nginx.conf
