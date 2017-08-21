#ifndef PLUGINS_LIMIT_RATE_CONFIGURATION_H_
#define PLUGINS_LIMIT_RATE_CONFIGURATION_H_
#include "common.h"

struct cmp_str{
	bool operator()(char const *a, char const *b) {
		return strcmp(a,b) < 0;
	}
};

class Configuration {
public:
	explicit Configuration() {};

	bool Parse(const std::string fname);

	std::map<const char *, uint64_t, cmp_str> limitconf;

	~Configuration() {
		std::map<const char *, uint64_t>::iterator it;
        it = limitconf.begin(); 
		//for( it= limitconf.begin(); it != limitconf.end(); it++) {
		//	free((void *)it->first);
		//}
        int i = 0;
        while(i != 24) {
            free((void *)it->first);
            i++;
            it++;
        }
	}
private:
	DISALLOW_COPY_AND_ASSIGN(Configuration);
};

#endif
