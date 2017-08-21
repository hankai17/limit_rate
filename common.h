#ifndef PLUGINS_LIMIT_RATE_COMMON_H_
#define PLUGINS_LIMIT_RATE_COMMON_H_

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <map>

#define PLUGIN_NAME "limit_rate"
#define DEBUG_LOG(fmt, ...) TSDebug(PLUGIN_NAME, "[%s:%d] %s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define ERROR_LOG(fmt, ...) TSError("[%s:%d] %s(): " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__)   

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)

#endif 
