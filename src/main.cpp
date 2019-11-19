#include <Arduino.h>
// #include "fc_config.h"
#include "asyncserver.h"
#include "timehelper.h"
#include "asynctcphelper.h"
#include "thermocouplehelper.h"
#include "thingspeakhelper.h"

#define DEBUGPORT Serial

// #define RELEASE

#ifndef RELEASE
#define DEBUGLOG(fmt, ...)                   \
  {                                          \
    static const char pfmt[] PROGMEM = fmt;  \
    DEBUGPORT.printf_P(pfmt, ##__VA_ARGS__); \
  }
#else
#define DEBUGLOG(...)
#endif

void setup()
{
  Serial.begin(115200);
  // Serial.setDebugOutput(true);

  AsyncServersetup();

  if (wifiMode == WIFI_STA)
  {
    Timesetup();
    AsyncTcpSetup();
    Thermocouplesetup();
    Thingspeaksetup();
  }

  DEBUGLOG("Setup completed!\r\n");
}

void loop()
{
  AsyncServerloop();

  if (WiFi.status() == WL_CONNECTED)
  {
    Timeloop();
    Thermocoupleloop();
  }

  // handle its own wifi status
  Thingspeakloop();
}
