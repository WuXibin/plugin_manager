Name:           %(echo nginx_adfront${SUFFIX})
Version:        %{_version}
Release:        %{_release}
Summary:        Nginx with plugin manager to manage C++ dynamic library.

Group:	        adplateform	
License:        adplateform 	
URL:	        %{_svn_path} 	

BuildRequires: protobuf >= 5.0.0-1
Requires: protobuf >= 5.0.0-1

%description

%prep
cd $OLDPWD
./configure --add-module=module_adfront --add-module=module_adserver --with-http_realip_module

%build
make -C $OLDPWD/module_adfront/plugin_manager
make -C $OLDPWD

%install
#copy plugin manager header files
mkdir -p $PWD/%{_prefix}/include/plugin_manager

cp $OLDPWD/module_adfront/plugin_manager/plugin.h $PWD/%{_prefix}/include/plugin_manager
cp $OLDPWD/module_adfront/plugin_manager/plugin_config.h $PWD/%{_prefix}/include/plugin_manager
cp $OLDPWD/module_adfront/plugin_manager/plugin_manager.conf.pb.h $PWD/%{_prefix}/include/plugin_manager

#copy plugin manager dynamic library
mkdir -p $PWD/%{_prefix}/lib64
cp $OLDPWD/module_adfront/plugin_manager/libplugin_manager.so $PWD/%{_prefix}/lib64


#copy Nginx bin and conf
mkdir -p $PWD/%{_prefix}/adplatform/nginx_adfront
mkdir -p $PWD/%{_prefix}/adplatform/nginx_adfront/sbin
mkdir -p $PWD/%{_prefix}/adplatform/nginx_adfront/logs
mkdir -p $PWD/%{_prefix}/adplatform/nginx_adfront/plugin_manager

cp $OLDPWD/objs/nginx $PWD/%{_prefix}/adplatform/nginx_adfront/sbin
cp -r $OLDPWD/conf $PWD/%{_prefix}/adplatform/nginx_adfront

%post
/sbin/ldconfig

%clean

%files
%{_prefix}

%changelog
