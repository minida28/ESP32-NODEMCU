// this example is public domain. enjoy!
// www.ladyada.net/learn/sensors/thermocouple

#include "max6675.h"
// #include "thermocouplehelper.h"
#include "asyncserver.h"
#include <ArduinoOTA.h>
#ifdef ESP32
#include <FS.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <Ticker.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266mDNS.h>
#endif
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <StreamString.h>
// #include "AsyncJson.h"
#include "fc_config.h"
#include "progmemmatrix.h"
#include <ArduinoJson.h>

// #include "asyncserver.h"
#include "timehelper.h"
#include "asynctcphelper.h"

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

// int thermoDO = 4;
// int thermoCS = 5;
// int thermoCLK = 6;

int thermoDO = 19;
int thermoCS = 18;
int thermoCLK = 5;

MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);
// int vccPin = 3;
// int gndPin = 2;

bool newTemp;
int pinLed = 2;
double thermocoupleTemp;

Ticker tickerPrintTemp;
Ticker tickerBlinkLed;

void BlinkLed(int state)
{
    digitalWrite(pinLed, state);
}

void printTemp()
{
    // DEBUGLOG("C = %.2f, F = %.2f\r\n", thermocouple.readCelsius(), thermocouple.readFahrenheit());
    // DEBUGLOG("Temp|%.2f\r\n", thermocouple.readCelsius());

    newTemp = true;
}

void sendThermocouple(uint8_t mode)
{
    DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

    time_t now;

    // struct tm *ptm;

    now = time(nullptr);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        // Serial.println("Failed to obtain time");
        // return;
        now = 0;
    }

    char bufTemp[10];

    dtostrf(thermocouple.readCelsius(), 0, 2, bufTemp);
    // sprintf(bufTemp, "%.2f", thermocouple.readCelsius());

    const char *temp = bufTemp;

    StaticJsonDocument<256> root;
    root["node"] = _config.hostname;
    root["utc"] = (uint32_t)now;
    root["temp"] = temp;

    size_t len = measureJson(root);
    char buf[len + 1];
    serializeJson(root, buf, len + 1);

    if (mode == 0)
    {
        //
    }
    else if (mode == 1)
    {
        // events.send(buf, "thermocouple");
    }
    else if (mode == 2)
    {
        int numClient = ws.count();
        DEBUGLOG("Number of Websocket client: %d\r\n", numClient);

        if (numClient)
        {
            ws.textAll(buf);
        }
        else
        {
            DEBUGLOG("Client is no longer available.\r\n");
        }
    }
}

void SendTempViaTcp()

{

    // if (WiFi.status() != WL_CONNECTED)
    //     return;

    char bufTemp[256];
    // dtostrf(thermocouple.readCelsius(), 0, 2, bufTemp);
    // sprintf("%.2f\r\n", clientID);

    time_t now;

    // struct tm *ptm;

    now = time(nullptr);

    // ptm = localtime(&now);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        // Serial.println("Failed to obtain time");
        DEBUGLOG("Failed to obtain time\r\n");
        // return;
        now = 0;
    }

    char bufEpoch64[20];

    dtostrf(now, 0, 0, bufEpoch64);

    // sprintf(bufTemp, "%s,%s,%.2f\r\n", _config.hostname, bufEpoch64, thermocouple.readCelsius());
    sprintf(bufTemp, "{TIMEPLOT|data|%s|T|%.2f}\r\n", _config.hostname, thermocouple.readCelsius());

    bool clientAvailable = false;

    for (std::vector<AsyncClient *>::iterator it = clients.begin(); it != clients.end(); ++it)
    {
        // *it;

        AsyncClient *client;
        client = *it;

        if (client->space() > 32 && client->canSend())
        {
            DEBUGLOG("Send temperature via TCP in Megunolink format\r\n");

            client->add(bufTemp, strlen(bufTemp));
            client->send();

            clientAvailable = true;
        }
    }

    if (clientAvailable)
    {
        DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);
        digitalWrite(pinLed, 1);
        tickerBlinkLed.once_ms(25, BlinkLed, 0);
    }

    // digitalWrite(pinLed, 1);
    // tickerBlinkLed.once_ms(25, BlinkLed, 0);
}

void Thermocouplesetup()
{
    DEBUGLOG("Setup MAX6675 Thermocouple...\r\n");

    //   Serial.begin(9600);
    // use Arduino pins
    // pinMode(vccPin, OUTPUT);
    // digitalWrite(vccPin, HIGH);
    // pinMode(gndPin, OUTPUT);
    // digitalWrite(gndPin, LOW);

    // Serial.println("MAX6675 test");
    // wait for MAX chip to stabilize
    // delay(500);

    // pinMode(pinLed, OUTPUT);

    tickerPrintTemp.attach(1, printTemp);
}

void Thermocoupleloop()
{
    if (sendThermocoupleFlag)
    {
        sendThermocoupleFlag = false;

        sendThermocouple(2);
    }

    if (newTemp)
    {
        newTemp = false;

        thermocoupleTemp = thermocouple.readCelsius();

        // DEBUGLOG("Temp|%.2f\r\n", thermocouple.readCelsius());
        DEBUGLOG("%u,%s,%.2f\r\n", (uint32_t)now, _config.hostname, thermocoupleTemp);
        DEBUGLOG("{TIMEPLOT|data|%s|T|%.2f}\r\n", _config.hostname, thermocoupleTemp);
        SendTempViaTcp();
    }
}