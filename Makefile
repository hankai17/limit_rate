PLUGIN_NAME=limit_rate
PLUGIN_VERSION=1.1.0
SOURCE_FILES=ratelimiter.cc configuration.cc limit_rate.cc 

TSXS=$(ATS_INSTALL_PREFIX)/bin/tsxs

all:
	$(TSXS) $(INCLUDES) -o $(PLUGIN_NAME).so $(SOURCE_FILES)
