#include "ThingSpeak.h"
// #include "secrets.h"
#include <WiFi.h>
#include <Ticker.h>
#include "fc_config.h"
#include "thermocouplehelper.h"
// #include "asyncserver.h"

// #define TIMEOUT_MS_SERVERRESPONSE 2000  // Wait up to five seconds for server to respond

#define DEBUGPORT Serial

// #define RELEASE

#ifndef RELEASE
#define DEBUGLOG(fmt, ...)                       \
    {                                            \
        static const char pfmt[] PROGMEM = fmt;  \
        DEBUGPORT.printf_P(pfmt, ##__VA_ARGS__); \
    }
#else
#define DEBUGLOG(...)
#endif

#define SECRET_CH_ID 914383                    // replace 0000000 with your channel number
#define SECRET_WRITE_APIKEY "DE0M29UW2YVMOA8Z" // replace XYZ with your channel write API Key

int keyIndex = 0; // your network key Index number (needed only for WEP)
WiFiClient client;

unsigned long myChannelNumber = SECRET_CH_ID;
const char *myWriteAPIKey = SECRET_WRITE_APIKEY;
bool updateThingspeakFlag = false;

int pinLed1 = 2;

Ticker tickerUpdateThingspeak;
Ticker tickerBlinkLed1;

void BlinkLed1(int state)
{
    digitalWrite(pinLed1, state);
}

void UpdateFlag()
{
    updateThingspeakFlag = true;
}

void UpdateThingspeak()
{
    // set the fields with the values
    ThingSpeak.setField(1, (float)thermocoupleTemp);

    // set the status
    ThingSpeak.setStatus(_config.hostname);

    int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (x == 200)
    {
        DEBUGLOG("\r\nTHINGSPEAK update successful.\r\n");
        digitalWrite(pinLed1, 1);
        tickerBlinkLed1.once_ms(500, BlinkLed1, 0);
    }
    else
    {
        // char bufHttpCode[5];
        // itoa(x, bufHttpCode, 10);
        DEBUGLOG("\r\nProblem updating THINGSPEAK. HTTP error code %i\r\n", x);
    }

    tickerUpdateThingspeak.once(15, UpdateFlag);
}

void Thingspeaksetup()
{
    DEBUGLOG("\r\nSetting-up Thingspeak...\r\n\r\n");

    pinMode(pinLed1, OUTPUT);

    ThingSpeak.begin(client); // Initialize ThingSpeak
}

void Thingspeakloop()
{
    // int currWifiStatus = WiFi.status();
    // static int oldWifiStatus = false;
    // if (oldWifiStatus != currWifiStatus)
    // {
    //     oldWifiStatus = currWifiStatus;

    //     if (currWifiStatus == WL_CONNECTED)
    //     {
    //         if (tickerUpdateThingspeak.active() == false)
    //         {
    //             tickerUpdateThingspeak.attach(20, UpdateFlag);
    //         }
    //     }
    //     else
    //     {
    //         if (tickerUpdateThingspeak.active() == true)
    //         {
    //             tickerUpdateThingspeak.detach();
    //         }
    //     }
    // }

    int currWifiStatus = WiFi.status();
    static int oldWifiStatus = false;
    if (oldWifiStatus != currWifiStatus)
    {
        oldWifiStatus = currWifiStatus;

        if (currWifiStatus == WL_CONNECTED)
        {
            tickerUpdateThingspeak.once(15, UpdateFlag);
        }
    }

    if (updateThingspeakFlag)
    {
        updateThingspeakFlag = false;
        UpdateThingspeak();
    }
}