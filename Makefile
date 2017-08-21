#该Makefile文件以tsxs命令来编译和安装
#different as each plugin                                                                                                                                                          
PLUGIN_NAME=limit_rate
PLUGIN_VERSION=1.1.0
#多个.cc和.c文件直接追加到后面，以空格分开
SOURCE_FILES=ratelimiter.cc configuration.cc limit_rate.cc 

#common
#通常我会将自己写的插件放到ATS源码的example目录下面编译和运行，所以加上该目录以便于找到ts目录下面的头文件
#如果ts目录已安装，可以不顶用INCLUDE目录
INCLUDES=-I. -I../../lib/ts
ATS_INSTALL_PREFIX=/opt/ats
PLUGIN_INSTALL_PREFIX=$(ATS_INSTALL_PREFIX)/libexec/trafficserver/
TSXS=$(ATS_INSTALL_PREFIX)/bin/tsxs

all:
	$(TSXS) $(INCLUDES) -o $(PLUGIN_NAME).so.$(PLUGIN_VERSION) $(SOURCE_FILES)

install:
	$(TSXS) -i -o $(PLUGIN_NAME).so.$(PLUGIN_VERSION)

clean:
	rm -rf *.lo $(PLUGIN_NAME).so.$(PLUGIN_VERSION)
