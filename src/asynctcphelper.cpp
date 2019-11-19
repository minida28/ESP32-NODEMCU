// #include <ESP8266WiFi.h>
// #include <ESPAsyncTCP.h>
// #include <DNSServer.h>
// #include "asynctcphelper.h"
#include "asyncserver.h"
#include <vector>

#include "config.h"

#define TCP_PORT 7050

// #include "asynctcphelper.h"

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

bool bufferAsyncTcpIsUpdated;

// static DNSServer DNS;

std::vector<AsyncClient *> clients; // a list to hold all clients

char bufferAsyncTcp[256];
size_t lenBufAsyncTcp;

/* clients events */
static void handleError(void *arg, AsyncClient *client, int8_t error)
{
    Serial.printf("\r\n connection error %s from client %s \r\n", client->errorToString(error), client->remoteIP().toString().c_str());

    //--- example handler
    // https://github.com/me-no-dev/AsyncTCP/issues/52#issuecomment-510842311

    // client->stop(); // close connection
    client->close(true); // force close connection
    client->free();
}

static void handleDisconnect(void *arg, AsyncClient *client)
{
    Serial.printf("\r\n client %s disconnected \r\n", client->remoteIP().toString().c_str());

    //--- example handler
    // https://github.com/me-no-dev/AsyncTCP/issues/52#issuecomment-510842311

    // client->stop(); // close connection
    client->close(true); // force close connection
    client->free();
}

static void handleTimeOut(void *arg, AsyncClient *client, uint32_t time)
{
    Serial.printf("\r\n client ACK timeout ip: %s, timeout: %d \r\n", client->remoteIP().toString().c_str(), time);

    //--- example handler
    // https://github.com/me-no-dev/AsyncTCP/issues/52#issuecomment-510842311

    // client->stop(); // close connection
    client->close(true); // force close connection
    client->free();
}

static void handleData(void *arg, AsyncClient *client, void *data, size_t len)
{
    // Serial.printf("\r\n data received from client %s, length: %d \r\n", client->remoteIP().toString().c_str(), len);
    // Serial.write((uint8_t*)data, len);

    const char *str = (const char *)data;
    lenBufAsyncTcp = len;
    // Serial.print(str, HEX);
    // Serial.print(" -> ");

    // size_t i = 0;
    // while (i < len)
    // {
    //     const char c  = str[i];
    //     Serial.print((uint8_t)c, HEX);
    //     Serial.print(" ");
    //     i++;
    // }

    DEBUGLOG("\r\nData received ");

    for (size_t i = 0; i < len; i++)
    {
        const char c = str[i];
        // Serial.print((uint8_t)c, HEX);
        bufferAsyncTcp[i] = c;

        DEBUGLOG("%02X ", bufferAsyncTcp[i]);

        if (i == len - 1)
        {
            bufferAsyncTcp[len] = '\0';
            DEBUGLOG("\r\n");
        }
    }

    // DEBUGLOG("received %s\r\n", bufferAsyncTcp);

    // char bufPrefix[len + 1];
    // strlcpy((uint8_t*)data, bufPrefix, sizeof((uint8_t*)data));
    // strncpy()
    // Serial.print(bufPrefix);

    // reply to client
    if (false)
    {
        if (client->space() > 32 && client->canSend())
        {
            // char reply[32];
            // sprintf(reply, "this is from %s\r\n", _config.hostname);
            // client->add(reply, strlen(reply));
            // client->send();

            char buf[] = {0x01, 0x04, 0x04, 0x43, 0x66, 0x33, 0x34, 0x1B, 0x38};

            client->add(buf, 9);
            client->send();
        }
    }

    bufferAsyncTcpIsUpdated = true;
}

/* server events */
static void handleNewClient(void *arg, AsyncClient *client)
{
    DEBUGLOG("new client has been connected to server, ip: %s\r\n", client->remoteIP().toString().c_str());

    // add to list
    clients.push_back(client);

    // register events
    client->onData(&handleData, NULL);
    client->onError(&handleError, NULL);
    client->onDisconnect(&handleDisconnect, NULL);
    client->onTimeout(&handleTimeOut, NULL);
}

// AsyncServer *asyncTcpServer = new AsyncServer(TCP_PORT); // start listening on tcp port 7050

void AsyncTcpSetup()
{

    // start dns server
    // if (!DNS.start(DNS_PORT, SERVER_HOST_NAME, WiFi.softAPIP()))
    // 	Serial.printf("\n failed to start dns service \n");

    AsyncServer *asyncTcpServer = new AsyncServer(TCP_PORT); // start listening on tcp port 7050
    asyncTcpServer->onClient(&handleNewClient, asyncTcpServer);
    asyncTcpServer->begin();
}

void AsyncTcpLoop()
{
    // DNS.processNextRequest();
}