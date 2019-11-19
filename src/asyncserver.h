#ifndef asyncserver_h
#define asyncserver_h

// #include "fc_config.h"
#include <ESPAsyncWebServer.h>

// extern strConfig _config;

extern AsyncWebSocket ws;

extern int wifiMode;

extern bool wifiGotIpFlag;
extern bool sendDateTimeFlag;
extern bool sendThermocoupleFlag;

void AsyncServersetup();
void AsyncServerloop();


#endif




