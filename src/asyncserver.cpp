#include "asyncserver.h"
#include <ArduinoOTA.h>
#ifdef ESP32
#include <FS.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <Ticker.h>
#include <NetBIOS.h>
#include <ESP32SSDP.h>
#include <DNSServer.h>
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
#include <rom/rtc.h>

#define uS_TO_S_FACTOR 1000000 /* Conversion factor for micro seconds to seconds */

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

strConfig _config;
strApConfig _configAP; // Static AP config settings
strHTTPAuth _httpAuth;

Ticker _secondTk;
Ticker restartESP;

//FS* _fs;
unsigned long wifiDisconnectedSince = 0;
String _browserMD5 = "";
uint32_t _updateSize = 0;

// bool autoConnect = true;
bool autoReconnect = true;

int wifiMode = WIFI_AP;

// bool eventsourceTriggered = false;
// bool wsConnected = false;
// bool configFileNetworkUpdatedFlag = false;
// bool configFileLocationUpdated = false;
// bool configFileTimeUpdated = false;

// WiFiEventHandler onStationModeConnectedHandler, onStationModeGotIPHandler, onStationModeDisconnectedHandler;
// WiFiEventHandler stationConnectedHandler;
// WiFiEventHandler stationDisconnectedHandler;
// WiFiEventHandler probeRequestPrintHandler;
// WiFiEventHandler probeRequestBlinkHandler;

bool wifiGotIpFlag = false;
// bool wifiDisconnectedFlag = false;
// bool stationConnectedFlag = false;
// bool stationDisconnectedFlag = false;

// bool firmwareUpdated = false;

DNSServer dnsServer;

// SKETCH BEGIN
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

bool sendDateTimeFlag = false;
bool sendThermocoupleFlag = false;
bool sendFreeHeapStatusFlag = false;

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    client->ping();
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
  }
  else if (type == WS_EVT_ERROR)
  {
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
  }
  else if (type == WS_EVT_PONG)
  {
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char *)data : "");
  }
  else if (type == WS_EVT_DATA)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    String msg = "";
    if (info->final && info->index == 0 && info->len == len)
    {
      //the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

      if (info->opcode == WS_TEXT)
      {
        for (size_t i = 0; i < info->len; i++)
        {
          msg += (char)data[i];
        }
      }
      else
      {
        char buff[3];
        for (size_t i = 0; i < info->len; i++)
        {
          sprintf(buff, "%02x ", (uint8_t)data[i]);
          msg += buff;
        }
      }
      Serial.printf("%s\n", msg.c_str());

      if (info->opcode == WS_TEXT)
      {
        // client->text("I got your text message");
      }

      else
      {
        // client->binary("I got your binary message");
      }
    }
    else
    {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if (info->index == 0)
      {
        if (info->num == 0)
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, info->index + len);

      if (info->opcode == WS_TEXT)
      {
        for (size_t i = 0; i < len; i++)
        {
          msg += (char)data[i];
        }
      }
      else
      {
        char buff[3];
        for (size_t i = 0; i < len; i++)
        {
          sprintf(buff, "%02x ", (uint8_t)data[i]);
          msg += buff;
        }
      }
      Serial.printf("%s\n", msg.c_str());

      if ((info->index + len) == info->len)
      {
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if (info->final)
        {
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
          if (info->message_opcode == WS_TEXT)
          {
            // client->text("I got your text message");
          }
          else
          {
            // client->binary("I got your binary message");
          }
        }
      }
    }

    // code
    // if (strncmp_P((char *)data, pgm_schedulepagesholat, strlen_P(pgm_schedulepagesholat)) == 0)
    if (msg == "thermocouple")
    {
      // clientVisitSholatTimePage = true;
      sendThermocoupleFlag = true;
    }
    else if (msg == "/status/datetime")
    {
      // clientVisitSholatTimePage = true;
      sendDateTimeFlag = true;
    }
    else if (msg == "freeheap")
    {
      // clientVisitSholatTimePage = true;
      sendFreeHeapStatusFlag = true;
    }
  }
}

bool load_config_network()
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  File file = SPIFFS.open(FPSTR(pgm_configfilenetwork), "r");
  if (!file)
  {
    DEBUGLOG("Failed to open config file\r\n");
    file.close();
    return false;
  }

  // size_t size = file.size();
  DEBUGLOG("JSON file size: %d bytes\r\n", file.size());

  StaticJsonDocument<512> root;
  DeserializationError error = deserializeJson(root, file);

  file.close();

  if (error)
  {
    DEBUGLOG("Failed to parse config NETWORK file\r\n");
    return false;
  }

#ifndef RELEASE
  serializeJsonPretty(root, DEBUGPORT);
  DEBUGLOG("\r\n");
#endif

  // strlcpy(_config.hostname, root[FPSTR(pgm_hostname)], sizeof(_config.hostname));
  strlcpy(_config.mode, root[FPSTR(pgm_mode)], sizeof(_config.mode));
  strlcpy(_config.ssid, root[FPSTR(pgm_ssid)], sizeof(_config.ssid));
  strlcpy(_config.password, root[FPSTR(pgm_password)], sizeof(_config.password));
  _config.dhcp = root[FPSTR(pgm_dhcp)];
  strlcpy(_config.static_ip, root[FPSTR(pgm_static_ip)], sizeof(_config.static_ip));
  strlcpy(_config.netmask, root[FPSTR(pgm_netmask)], sizeof(_config.netmask));
  strlcpy(_config.gateway, root[FPSTR(pgm_gateway)], sizeof(_config.gateway));
  strlcpy(_config.dns0, root[FPSTR(pgm_dns0)], sizeof(_config.dns0));
  strlcpy(_config.dns1, root[FPSTR(pgm_dns1)], sizeof(_config.dns1));

  DEBUGLOG("\r\nNetwork settings loaded successfully.\r\n");
  DEBUGLOG("mode: %s\r\n", _config.mode);
  DEBUGLOG("hostname: %s\r\n", _config.hostname);
  DEBUGLOG("ssid: %s\r\n", _config.ssid);
  DEBUGLOG("pass: %s\r\n", _config.password);
  DEBUGLOG("dhcp: %d\r\n", _config.dhcp);
  DEBUGLOG("static_ip: %s\r\n", _config.static_ip);
  DEBUGLOG("netmask: %s\r\n", _config.netmask);
  DEBUGLOG("gateway: %s\r\n", _config.gateway);
  DEBUGLOG("dns0: %s\r\n", _config.dns0);
  DEBUGLOG("dns1: %s\r\n", _config.dns1);
  DEBUGLOG("\r\n");

  return true;
}

//*************************
// SAVE NETWORK CONFIG
//*************************
bool save_config_network()
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  StaticJsonDocument<1024> json;
  // json[FPSTR(pgm_hostname)] = _config.hostname;
  json[FPSTR(pgm_mode)] = _config.mode;
  json[FPSTR(pgm_ssid)] = _config.ssid;
  json[FPSTR(pgm_password)] = _config.password;
  json[FPSTR(pgm_dhcp)] = _config.dhcp;
  json[FPSTR(pgm_static_ip)] = _config.static_ip;
  json[FPSTR(pgm_netmask)] = _config.netmask;
  json[FPSTR(pgm_gateway)] = _config.gateway;
  json[FPSTR(pgm_dns0)] = _config.dns0;
  json[FPSTR(pgm_dns1)] = _config.dns1;

  //TODO add AP data to html
  File file = SPIFFS.open(FPSTR(pgm_configfilenetwork), "w");

#ifndef RELEASE
  serializeJsonPretty(json, DEBUGPORT);
  DEBUGLOG("\r\n");
#endif

  // EEPROM_write_char(eeprom_wifi_ssid_start, eeprom_wifi_ssid_size, _config.ssid);
  // EEPROM_write_char(eeprom_wifi_password_start, eeprom_wifi_password_size, wifi_password);

  serializeJsonPretty(json, file);
  file.flush();
  file.close();
  return true;
}

void onWifiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
  DEBUGLOG("\r\nWifi got IP: %s\r\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
  wifiGotIpFlag = true;
  WiFi.setHostname(_config.hostname);
  WiFi.setAutoReconnect(autoReconnect);
  WiFi.setAutoConnect(true);
}

void restart_1()
{
  // digitalWrite(0, HIGH); //GPIO0
  // digitalWrite(2, HIGH); //GPIO2
  // digitalWrite(15, LOW); //GPIO15
  ESP.restart();
}

void restart_esp(AsyncWebServerRequest *request)
{
  request->send_P(200, "text/html", Page_WaitAndReload);
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);
  SPIFFS.end(); // SPIFFS.end();

  /*
    GPIO 0  GPIO 2  GPIO 15
    UART Download Mode (Programming)  0       1       0
    Flash Startup (Normal)            1       1       0
    SD - Card Boot                      0       0       1
  */

  //WiFi.forceSleepBegin(); wdt_reset(); ESP.restart(); while(1)wdt_reset();

  ESP.restart();

  // restartESP.attach(1.0f, restart_1); // Task to run periodic things every second
}

void send_wwwauth_configuration_values_html(AsyncWebServerRequest *request)
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  StaticJsonDocument<512> root;
  if (_httpAuth.auth)
  {
    root[FPSTR(pgm_wwwauth)] = true;
  }
  else
  {
    root[FPSTR(pgm_wwwauth)] = false;
  }
  root[FPSTR(pgm_wwwuser)] = _httpAuth.wwwUsername;
  root[FPSTR(pgm_wwwpass)] = _httpAuth.wwwPassword;

  AsyncResponseStream *response = request->beginResponseStream("text/plain");
  serializeJson(root, *response);
  request->send(response);
}

bool saveHTTPAuth()
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  StaticJsonDocument<512> json;
  json[FPSTR(pgm_wwwauth)] = _httpAuth.auth;
  json[FPSTR(pgm_wwwuser)] = _httpAuth.wwwUsername;
  json[FPSTR(pgm_wwwpass)] = _httpAuth.wwwPassword;

  //TODO add AP data to html

  const char *fileName = pgm_SECRET_FILE;

  File file = SPIFFS.open(fileName, "w");
  if (!file)
  {
    DEBUGLOG("Failed to open %s file for writing\r\n", fileName);
    file.close();
    return false;
  }

#ifndef RELEASEASYNCWS
  serializeJsonPretty(json, DEBUGPORT);
  DEBUGLOG("\r\n");
#endif // RELEASE

  serializeJsonPretty(json, file);
  file.flush();
  file.close();
  return true;
}

void send_wwwauth_configuration_html(AsyncWebServerRequest *request)
{
  DEBUGLOG("%s %d\r\n", __FUNCTION__, request->args());

  //List all parameters
  int params = request->params();
  if (params)
  {
    for (int i = 0; i < params; i++)
    {
      AsyncWebParameter *p = request->getParam(i);
      if (p->isFile())
      { //p->isPost() is also true
        DEBUGLOG("FILE[%s]: %s, size: %u\r\n", p->name().c_str(), p->value().c_str(), p->size());
      }
      else if (p->isPost())
      {
        DEBUGLOG("POST[%s]: %s\r\n", p->name().c_str(), p->value().c_str());
        //      if (p->name() == "wwwauth") {
        //        _httpAuth.auth = p->value();
        //      }
        if (request->hasParam("wwwauth", true))
        {
          if (p->name() == "wwwauth")
          {
            _httpAuth.auth = p->value();
          }
          if (p->name() == "wwwuser")
          {
            strlcpy(_httpAuth.wwwUsername, p->value().c_str(), sizeof(_httpAuth.wwwUsername));
          }
          if (p->name() == "wwwpass")
          {
            strlcpy(_httpAuth.wwwPassword, p->value().c_str(), sizeof(_httpAuth.wwwPassword));
          }
        }
      }
      else
      {
        DEBUGLOG("GET[%s]: %s\r\n", p->name().c_str(), p->value().c_str());
      }
    }
  }
  else
  {
    _httpAuth.auth = false;
  }

  //save settings
  saveHTTPAuth();

  // //Check if POST (but not File) parameter exists
  // if (request->hasParam("wwwauth", true))
  //   AsyncWebParameter* p = request->getParam("wwwauth", true);

  // //Check if POST (but not File) parameter exists
  // if (request->hasParam("wwwuser", true))
  //   AsyncWebParameter* p = request->getParam("wwwuser", true);

  // //Check if POST (but not File) parameter exists
  // if (request->hasParam("wwwpass", true))
  //   AsyncWebParameter* p = request->getParam("wwwpass", true);

  request->send(SPIFFS, request->url());
}

void send_update_firmware_values_html(AsyncWebServerRequest *request)
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);
  String values = "";
  uint32_t freeSketchSpace = ESP.getFreeSketchSpace();
  uint32_t maxSketchSpace = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
  //bool updateOK = Update.begin(maxSketchSpace);
  bool updateOK = maxSketchSpace < freeSketchSpace;
  StreamString result;
  Update.printError(result);
  DEBUGLOG("--FreeSketchSpace: %d\r\n", freeSketchSpace);
  DEBUGLOG("--MaxSketchSpace: %d\r\n", maxSketchSpace);
  DEBUGLOG("--Update error = %s\r\n", result.c_str());
  values += "remupd|" + (String)((updateOK) ? "OK" : "ERROR") + "|div\n";

  if (Update.hasError())
  {
    result.trim();
    values += "remupdResult|" + result + "|div\n";
  }
  else
  {
    values += "remupdResult||div\n";
  }

  request->send(200, "text/plain", values);
}

void setUpdateMD5(AsyncWebServerRequest *request)
{
  DEBUGLOG("%s %d\r\n", __FUNCTION__, request->args());

  const char *browserMD5 = nullptr;

  int params = request->params();
  if (params)
  {
    for (int i = 0; i < params; i++)
    {
      AsyncWebParameter *p = request->getParam(i);
      if (p->isFile())
      { //p->isPost() is also true
        DEBUGLOG("FILE[%s]: %s, size: %u\r\n", p->name().c_str(), p->value().c_str(), p->size());
      }
      else if (p->isPost())
      {
        DEBUGLOG("POST[%s]: %s\r\n", p->name().c_str(), p->value().c_str());
        if (request->hasParam("md5", true))
        {
          if (p->name() == "md5")
          {
            browserMD5 = p->value().c_str();
            Update.setMD5(browserMD5);
          }
          if (p->name() == "size")
          {
            _updateSize = atoi(p->value().c_str());
            DEBUGLOG("Update size: %u\r\n", _updateSize);
          }
        }
      }
      else
      {
        DEBUGLOG("GET[%s]: %s\r\n", p->name().c_str(), p->value().c_str());
        if (request->hasParam("md5"))
        {
          if (p->name() == "md5")
          {
            browserMD5 = p->value().c_str();
            Update.setMD5(browserMD5);
          }
          if (p->name() == "size")
          {
            _updateSize = atoi(p->value().c_str());
            DEBUGLOG("Update size: %u\r\n", _updateSize);
          }
        }
      }
    }

    if (browserMD5 != nullptr)
    {
      char buf[64] = "OK --> MD5: ";
      strncat(buf, browserMD5, sizeof(buf));
      request->send(200, "text/html", buf);
      return;
    }
    else
    {
      request->send_P(500, "text/html", PSTR("Error: MD5 is NULL"));
      return;
    }
  }
  request->send_P(200, "text/html", PSTR("Empty Parameter"));
}

void updateFirmware(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  // handler for the file upload, get's the sketch bytes, and writes
  // them through the Update object
  static unsigned long totalSize = 0;
  if (!index)
  { //UPLOAD_FILE_START
    // SPIFFS.end();
    // Update.runAsync(true);

    DEBUGLOG("Update start: %s\r\n", filename.c_str());
    uint32_t maxSketchSpace = ESP.getSketchSize();
    DEBUGLOG("Max free scketch space: %u\r\n", maxSketchSpace);
    DEBUGLOG("New scketch size: %u\r\n", _updateSize);
    if (_browserMD5 != NULL && _browserMD5 != "")
    {
      Update.setMD5(_browserMD5.c_str());
      DEBUGLOG("Hash from client: %s\r\n", _browserMD5.c_str());
    }
    if (!Update.begin(_updateSize))
    { //start with max available size
      Update.printError(DEBUGPORT);
    }
  }

  // Get upload file, continue if not start
  totalSize += len;
  DEBUGLOG(".");
  size_t written = Update.write(data, len);
  if (written != len)
  {
    DEBUGLOG("len = %d, written = %zu, totalSize = %lu\r\n", len, written, totalSize);
    //Update.printError(PRINTPORT);
    //return;
  }
  if (final)
  { // UPLOAD_FILE_END
    String updateHash;
    DEBUGLOG("\r\nApplying update...\r\n");
    if (Update.end(true))
    { //true to set the size to the current progress
      updateHash = Update.md5String();
      DEBUGLOG("Upload finished. Calculated MD5: %s\r\n", updateHash.c_str());
      DEBUGLOG("Update Success: %u\r\nRebooting...\r\n", request->contentLength());
      // SPIFFS.end();
      // WiFi.mode(WIFI_OFF);
      // restartESP.attach(1.0f, restart_1);
      // restartESP.attach(1.0f, reset_1);

      // firmwareUpdated = true;
      ESP.restart();
    }
    else
    {
      updateHash = Update.md5String();
      DEBUGLOG("Upload failed. Calculated MD5: %s\r\n", updateHash.c_str());
      Update.printError(Serial);
    }
  }

  //delay(2);
}

void sendHeap(uint8_t mode)
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  uint32_t heapsize = ESP.getHeapSize();
  uint32_t freeheap = ESP.getFreeHeap();
  uint32_t minfreeheap = ESP.getMinFreeHeap();
  uint32_t maxallocheap = ESP.getMaxAllocHeap();

  StaticJsonDocument<256> root;
  root["heapsize"] = heapsize;
  root["freeheap"] = freeheap;
  root["minfreeheap"] = minfreeheap;
  root["maxallocheap"] = maxallocheap;

  size_t len = measureJson(root);
  char buf[len + 1];
  serializeJson(root, buf, len + 1);

  if (mode == 0)
  {
    //
  }
  else if (mode == 1)
  {
    // events.send(buf, "heap");
  }
  else if (mode == 2)
  {
    if (ws.count())
    {
      ws.textAll(buf);
    }
    else
    {
      DEBUGLOG("wsClient is no longer available.\r\n");
    }
  }
}

int pgm_lastIndexOf(uint8_t c, const char *p)
{
  int last_index = -1; // -1 indicates no match
  uint8_t b;
  for (int i = 0; true; i++)
  {
    b = pgm_read_byte(p++);
    if (b == c)
      last_index = i;
    else if (b == 0)
      break;
  }
  return last_index;
}

bool save_system_info()
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  // const char* pathtofile = PSTR(pgm_filesystemoverview);

  // String fileName = FPSTR(pgm_systeminfofile);

  size_t fileLen = strlen_P(pgm_systeminfofile);
  char fileName[fileLen + 1];
  strcpy_P(fileName, pgm_systeminfofile);

  File file;
  if (!SPIFFS.exists(fileName))
  {
    file = SPIFFS.open(fileName, "w");
    if (!file)
    {
      DEBUGLOG("Failed to open %s file for writing\r\n", fileName);
      file.close();
      return false;
    }
    //create blank json file
    DEBUGLOG("Creating %s file for writing\r\n", fileName);
    file.print("{}");
    file.close();
  }
  //get existing json file
  file = SPIFFS.open(fileName, "w");
  if (!file)
  {
    DEBUGLOG("Failed to open %s file", fileName);
    return false;
  }

  const char *the_path = PSTR(__FILE__);
  // const char *_compiletime = PSTR(__TIME__);

  int slash_loc = pgm_lastIndexOf('/', the_path); // index of last '/'
  if (slash_loc < 0)
    slash_loc = pgm_lastIndexOf('\\', the_path); // or last '\' (windows, ugh)

  int dot_loc = pgm_lastIndexOf('.', the_path); // index of last '.'
  if (dot_loc < 0)
    dot_loc = pgm_lastIndexOf(0, the_path); // if no dot, return end of string

  int lenPath = strlen(the_path);
  int lenStrFileName;

  bool useFullPath = true;
  int start_loc = 0;

  if (useFullPath)
  {
    lenStrFileName = lenPath;
    start_loc = 0;
  }
  else
  {
    lenStrFileName = (lenPath - (slash_loc + 1));
    start_loc = slash_loc + 1;
  }

  char strFileName[lenStrFileName + 1];

  //Serial.println(lenFileName);
  //Serial.println(sizeof(fileName));

  int j = 0;
  for (int i = start_loc; i < lenPath; i++)
  {
    uint8_t b = pgm_read_byte(&the_path[i]);
    if (b != 0)
    {
      strFileName[j] = (char)b;
      //Serial.print(strFileName[j]);
      j++;
      if (j >= lenStrFileName)
      {
        break;
      }
    }
    else
    {
      break;
    }
  }
  //Serial.println();
  //Serial.println(j);
  strFileName[lenStrFileName] = '\0';

  //const char* _compiledate = PSTR(__DATE__);
  int lenCompileDate = strlen_P(PSTR(__DATE__));
  char compileDate[lenCompileDate + 1];
  strcpy_P(compileDate, PSTR(__DATE__));

  int lenCompileTime = strlen_P(PSTR(__TIME__));
  char compileTime[lenCompileTime + 1];
  strcpy_P(compileTime, PSTR(__TIME__));

  StaticJsonDocument<1024> root;

  // SPIFFS.info(fs_info);

  // root[FPSTR(pgm_totalbytes)] = fs_info.totalBytes;
  // root[FPSTR(pgm_usedbytes)] = fs_info.usedBytes;
  // root[FPSTR(pgm_blocksize)] = fs_info.blockSize;
  // root[FPSTR(pgm_pagesize)] = fs_info.pageSize;
  // root[FPSTR(pgm_maxopenfiles)] = fs_info.maxOpenFiles;
  // root[FPSTR(pgm_maxpathlength)] = fs_info.maxPathLength;

  root[FPSTR(pgm_filename)] = strFileName;
  root[FPSTR(pgm_compiledate)] = compileDate;
  root[FPSTR(pgm_compiletime)] = compileTime;
  // root[FPSTR(pgm_lastboot)] = getLastBootStr();
  uint64_t chipid;
  chipid = ESP.getEfuseMac(); //The chip ID is essentially its MAC address(length: 6 bytes).
                              // Serial.printf("ESP32 Chip ID = %04X", (uint16_t)(chipid >> 32)); //print High 2 bytes
                              // Serial.printf("%08X\n", (uint32_t)chipid);                       //print Low 4bytes.

  char bufChipId[23];
  // uint64_t chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes).
  uint16_t chip = (uint16_t)(chipid >> 32);
  snprintf(bufChipId, 23, "%04X%08X", chip, (uint32_t)chipid);
  root[FPSTR(pgm_chipid)] = bufChipId;

  root[FPSTR(pgm_chiprev)] = ESP.getChipRevision();
  root[FPSTR(pgm_cpufreq)] = ESP.getCpuFreqMHz();
  root[FPSTR(pgm_sketchsize)] = ESP.getSketchSize();
  root[FPSTR(pgm_freesketchspace)] = ESP.getFreeSketchSpace();
  // root[FPSTR(pgm_flashchipid)] = ESP.getFlashChipId();
  root[FPSTR(pgm_flashchipmode)] = ESP.getFlashChipMode();
  root[FPSTR(pgm_flashchipsize)] = ESP.getFlashChipSize();
  // root[FPSTR(pgm_flashchiprealsize)] = ESP.getFlashChipRealSize();
  root[FPSTR(pgm_flashchipspeed)] = ESP.getFlashChipSpeed();
  // root[FPSTR(pgm_cyclecount)] = ESP.getCycleCount();
  // root[FPSTR(pgm_corever)] = ESP.getFullVersion();
  root[FPSTR(pgm_sdkver)] = ESP.getSdkVersion();
  // root[FPSTR(pgm_bootmode)] = ESP.getBootMode();
  // root[FPSTR(pgm_bootversion)] = ESP.getBootVersion();
  // root[FPSTR(pgm_resetreason)] = ESP.getResetReason();
  root[FPSTR(pgm_resetreasoncpu0)] = FPSTR(resetreason_P[rtc_get_reset_reason(0)]);
  root[FPSTR(pgm_resetreasoncpu1)] = FPSTR(resetreason_P[rtc_get_reset_reason(1)]);
  root["maker"] = "Muhammad Iqbal";

  serializeJsonPretty(root, file);
  file.flush();
  file.close();
  return true;
}

void handleFileList(AsyncWebServerRequest *request)
{
  if (!request->hasArg("dir"))
  {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = request->arg("dir");
  DEBUGLOG("handleFileList: %s\r\n", path.c_str());
  File dir = SPIFFS.open(path);
  // File dir = SPIFFS.open(path);
  path = String();

  String output = "[";

  // while (dir.next())
  // {
  //   File entry = dir.open("r");
  //   if (true) //entry.name()!="secret.json") // Do not show secrets
  //   {
  //     if (output != "[")
  //       output += ',';
  //     bool isDir = false;
  //     output += "{\"type\":\"";
  //     output += (isDir) ? "dir" : "file";
  //     output += "\",\"name\":\"";
  //     output += String(entry.name()).substring(1);
  //     output += "\"}";
  //   }
  //   entry.close();
  // }

  output += "]";
  DEBUGLOG("%s\r\n", output.c_str());
  request->send(200, "application/json", output);
}

// void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
//     Serial.printf("Listing directory: %s\r\n", dirname);

//     File root = fs.open(dirname);
//     if(!root){
//         Serial.println("- failed to open directory");
//         return;
//     }
//     if(!root.isDirectory()){
//         Serial.println(" - not a directory");
//         return;
//     }

//     File file = root.openNextFile();
//     while(file){
//         if(file.isDirectory()){
//             Serial.print("  DIR : ");
//             Serial.println(file.name());
//             if(levels){
//                 listDir(fs, file.name(), levels -1);
//             }
//         } else {
//             Serial.print("  FILE: ");
//             Serial.print(file.name());
//             Serial.print("\tSIZE: ");
//             Serial.println(file.size());
//         }
//         file = root.openNextFile();
//     }
// }

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
  if (event == SYSTEM_EVENT_STA_GOT_IP)
  {
    DEBUGLOG("\r\nWifi connected! IP: %s\r\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
    wifiGotIpFlag = true;
    WiFi.setHostname(_config.hostname);
    WiFi.setAutoReconnect(autoReconnect);
    WiFi.setAutoConnect(true);
  }
  else if (event == SYSTEM_EVENT_STA_DISCONNECTED)
  {
    DEBUGLOG("\r\nWifi disconnected, reconnecting...\r\n");
    wifiGotIpFlag = false;
    WiFi.reconnect();
  }
}

void AsyncServersetup()
{
  DEBUGLOG("\r\n");

  const char *reasonCPU0 = PSTR(resetreason_P[rtc_get_reset_reason(0)]);
  DEBUGLOG("CPU0 reset reason: %s\r\n", reasonCPU0);
  const char *reasonCPU1 = PSTR(resetreason_P[rtc_get_reset_reason(0)]);
  DEBUGLOG("CPU1 reset reason: %s\r\n", reasonCPU1);

  SPIFFS.begin();

  // save_config_network();

  if (!load_config_network())
  { // Try to load configuration from file system
    //defaultConfig(); // Load defaults if any error

    save_config_network();
  }

  //Set the host name
  uint64_t macAddress = ESP.getEfuseMac();
  uint64_t macAddressTrunc = macAddress << 40;
  uint32_t chipID = macAddressTrunc >> 40;
  snprintf(_config.hostname, sizeof(_config.hostname) / sizeof(_config.hostname[0]), "ESP%06X", chipID);

  // WiFi.mode(WIFI_OFF);

  //   WiFi.softAPsetHostname(_config.hostname);
  // WiFi.setHostname(_config.hostname);

  // Set wifi mode
  wifiMode = WIFI_AP;
  if (strcmp(_config.mode, "STA") == 0)
    wifiMode = WIFI_STA;
  else if (strcmp(_config.mode, "AP_STA") == 0)
    wifiMode = WIFI_AP_STA;

  if (wifiMode == WIFI_AP)
  {
    DEBUGLOG("Start Wifi in AP mode.\r\n\r\n");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_config.hostname);
    WiFi.softAPsetHostname(_config.hostname);
  }
  else if (wifiMode == WIFI_STA)
  {
    DEBUGLOG("Start Wifi in STA mode.\r\n\r\n");

    WiFi.onEvent(WiFiEvent);

    // WiFi.onEvent(onWifiGotIP, WiFiEvent_t::SYSTEM_EVENT_STA_GOT_IP);
    // WiFi.onEvent(onWifiGotIP, WiFiEvent_t::SYSTEM_EVENT_STA_GOT_IP);

    WiFi.mode(WIFI_STA);
    WiFi.begin(_config.ssid, _config.password);
    if (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
      DEBUGLOG("STA: Failed!\r\n\r\n");
      WiFi.disconnect(false);
      delay(1000);
      WiFi.begin(_config.ssid, _config.password);
    }
  }
  else if (wifiMode == WIFI_AP_STA)
  {
    DEBUGLOG("Start Wifi in AP_STA mode.\r\n\r\n");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(_config.hostname);
    WiFi.softAPsetHostname(_config.hostname);
    WiFi.begin(_config.ssid, _config.password);
    if (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
      DEBUGLOG("STA: Failed!\r\n\r\n");
      WiFi.disconnect(false);
      delay(1000);
      WiFi.begin(_config.ssid, _config.password);
    }
  }

  // Setting Hostname must be after setting the wifi mode!
  // WiFi.setHostname(_config.hostname);

  //Send OTA events to the browser
  ArduinoOTA.onStart([]() { events.send("Update Start", "ota"); });
  ArduinoOTA.onEnd([]() { events.send("Update End", "ota"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    char p[32];
    sprintf(p, "Progress: %u%%\n", (progress / (total / 100)));
    events.send(p, "ota");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR)
      events.send("Auth Failed", "ota");
    else if (error == OTA_BEGIN_ERROR)
      events.send("Begin Failed", "ota");
    else if (error == OTA_CONNECT_ERROR)
      events.send("Connect Failed", "ota");
    else if (error == OTA_RECEIVE_ERROR)
      events.send("Recieve Failed", "ota");
    else if (error == OTA_END_ERROR)
      events.send("End Failed", "ota");
  });
  ArduinoOTA.setHostname(_config.hostname);
  ArduinoOTA.begin();

  if (wifiMode == WIFI_AP)
  {
    // dnsServer.start(53, "*", WiFi.softAPIP());
  }
  else if (wifiMode == WIFI_STA)
  {
    if (0)
    {
      
      DEBUGLOG("Starting NBNS...\r\n");
      NBNS.begin(_config.hostname);
      
      DEBUGLOG("Starting SSDP...\r\n");
      SSDP.setSchemaURL("description.xml");
      SSDP.setHTTPPort(80);
      SSDP.setDeviceType("upnp:rootdevice");
      // SSDP.setModelName(ssdp_modelName);
      // SSDP.setModelNumber(ssdp_modelNumber);

      // SSDP.setSchemaURL(FPSTR(pgm_descriptionxml));
      // SSDP.setHTTPPort(80);
      // SSDP.setDeviceType(FPSTR(pgm_upnprootdevice));
      //  SSDP.setModelName(_config.deviceName.c_str());
      //  SSDP.setModelNumber(FPSTR(modelNumber));
      SSDP.begin();

      server.on("/description.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
        DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

        File file = SPIFFS.open(FPSTR(pgm_descriptionxmlfile), "r");
        if (!file)
        {
          DEBUGLOG("Failed to open %s file\r\n", file.name());
          file.close();
          return;
        }

        size_t size = file.size();
        DEBUGLOG("%s file size: %d bytes\r\n", file.name(), size);

        // size_t allocatedSize = 1024;
        // if (size > allocatedSize)
        // {
        //   PRINT("WARNING, %s file size %d bytes is larger than allocatedSize %d bytes. Exiting...\r\n", file.name(), size, allocatedSize);
        //   file.close();
        //   return;
        // }

        // Allocate a buffer to store contents of the file
        char buf[size + 1];

        //copy file to buffer
        file.readBytes(buf, size);

        //add termination character at the end
        buf[size] = '\0';

        //close the file, save your memory, keep healthy :-)
        file.close();

        // DEBUGLOG("%s\r\n", buf);

        size_t lenBuf = size;
        DEBUGLOG("Template size: %d bytes\r\n", lenBuf);

        //convert IP address to char array
        // size_t len = strlen(WiFi.localIP().toString().c_str());
        // char URLBase[len + 1];
        // strlcpy(URLBase, WiFi.localIP().toString().c_str(), sizeof(URLBase));

        char strLocal[] = ".local";
        size_t len0 = strlen(strLocal);
        size_t len = strlen(_config.hostname);

        char URLBase[len0 + len + 1];
        strlcpy(URLBase, _config.hostname, sizeof(URLBase));

        strncat(URLBase, strLocal, sizeof(URLBase));

        lenBuf = lenBuf + strlen(URLBase);

        // const char *friendlyName = WiFi.hostname().toString().c_str();
        len = strlen(_config.hostname);
        char friendlyName[len + 1];
        strlcpy(friendlyName, _config.hostname, sizeof(friendlyName));

        lenBuf = lenBuf + strlen(friendlyName);

        char presentationURL[] = "/";

        lenBuf = lenBuf + strlen(presentationURL);

        uint32_t serialNumber = ESP.getEfuseMac();

        lenBuf = lenBuf + strlen(friendlyName);
#ifdef ESP32
        char modelName[] = "ESP32";
#elif defined(ESP8266)
      char modelName[] = "ESP8266EX";
#endif

        lenBuf = lenBuf + strlen(modelName);
        const char *modelNumber = friendlyName;

        lenBuf = lenBuf + strlen(modelNumber);

        lenBuf = lenBuf + 6;
        DEBUGLOG("Allocated size: %d bytes\r\n", lenBuf);

        StreamString output;

        if (output.reserve(lenBuf))
        {
          output.printf(buf,
                        URLBase,
                        friendlyName,
                        presentationURL,
                        serialNumber,
                        modelName,
                        modelNumber, //modelNumber
                        (uint8_t)((serialNumber >> 16) & 0xff),
                        (uint8_t)((serialNumber >> 8) & 0xff),
                        (uint8_t)serialNumber & 0xff);
          request->send(200, "text/xml", output);
        }
        else
        {
          request->send(500);
        }
      });
    }
  }

  DEBUGLOG("Starting mDNS responder...");
  // MDNS.addService("http", "tcp", 80);

  if (!MDNS.begin(_config.hostname))
  { // Start the mDNS responder for esp8266.local
    DEBUGLOG("[Failed]\r\n");
  }
  else
  {
    DEBUGLOG("[OK]\r\n");
    // MDNS.addService("http", "tcp", 80);
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("thermocouple", "tcp", 7050);
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  events.onConnect([](AsyncEventSourceClient *client) {
    client->send("hello!", NULL, millis(), 1000);
  });
  server.addHandler(&events);

#ifdef ESP32
  server.addHandler(new SPIFFSEditor(SPIFFS, "", ""));
#elif defined(ESP8266)
  server.addHandler(new SPIFFSEditor(http_username, http_password));
#endif

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  // server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");

  // Serve the file "/www/index-ap.htm" in AP, and the file "/www/index.htm" on STA
  server.rewrite("/", "/index.htm");
  server.rewrite("/index.htm", "/confignetwork.html").setFilter(ON_AP_FILTER);
  // server.serveStatic("/", SPIFFS, "/www/");
  server.serveStatic("/", SPIFFS, "/");

  server.onNotFound([](AsyncWebServerRequest *request) {
    DEBUGLOG("NOT_FOUND: ");
    if (request->method() == HTTP_GET)
    {
      DEBUGLOG("GET");
    }
    else if (request->method() == HTTP_POST)
    {
      DEBUGLOG("POST");
    }
    else if (request->method() == HTTP_DELETE)
    {
      DEBUGLOG("DELETE");
    }
    else if (request->method() == HTTP_PUT)
    {
      DEBUGLOG("PUT");
    }
    else if (request->method() == HTTP_PATCH)
    {
      DEBUGLOG("PATCH");
    }
    else if (request->method() == HTTP_HEAD)
    {
      DEBUGLOG("HEAD");
    }
    else if (request->method() == HTTP_OPTIONS)
    {
      DEBUGLOG("OPTIONS");
    }
    else
    {
      DEBUGLOG("UNKNOWN");
    }

    DEBUGLOG(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if (request->contentLength())
    {
      DEBUGLOG("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      DEBUGLOG("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for (i = 0; i < headers; i++)
    {
      AsyncWebHeader *h = request->getHeader(i);
      DEBUGLOG("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for (i = 0; i < params; i++)
    {
      AsyncWebParameter *p = request->getParam(i);
      if (p->isFile())
      {
        DEBUGLOG("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      }
      else if (p->isPost())
      {
        DEBUGLOG("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
      else
      {
        DEBUGLOG("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }

    request->send(404);
  });
  server.onFileUpload([](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index)
      Serial.printf("UploadStart: %s\n", filename.c_str());
    Serial.printf("%s", (const char *)data);
    if (final)
      Serial.printf("UploadEnd: %s (%u)\n", filename.c_str(), index + len);
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!index)
      Serial.printf("BodyStart: %u\n", total);
    Serial.printf("%s", (const char *)data);
    if (index + len == total)
      Serial.printf("BodyEnd: %u\n", total);
  });

  server.on("/status/network", [](AsyncWebServerRequest *request) {
    DEBUGLOG("%s\r\n", request->url().c_str());

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    // DynamicJsonDocument root(2048);
    StaticJsonDocument<1024> root;

    uint64_t chipid;
    chipid = ESP.getEfuseMac(); //The chip ID is essentially its MAC address(length: 6 bytes).
                                // Serial.printf("ESP32 Chip ID = %04X", (uint16_t)(chipid >> 32)); //print High 2 bytes
                                // Serial.printf("%08X\n", (uint32_t)chipid);                       //print Low 4bytes.

    char bufChipId[23];
    // uint64_t chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes).
    uint16_t chip = (uint16_t)(chipid >> 32);
    snprintf(bufChipId, sizeof(bufChipId) / sizeof(bufChipId[0]), "%04X%08X", chip, (uint32_t)chipid);
    root[FPSTR(pgm_chipid)] = bufChipId;

    // DEBUGLOG("mode: %d\r\n", WiFi.getMode());

    if (WiFi.getMode() == WIFI_AP)
      root[FPSTR(pgm_hostname)] = WiFi.softAPgetHostname();
    else if (WiFi.getMode() == WIFI_STA)
      root[FPSTR(pgm_hostname)] = WiFi.getHostname();
    else
      root[FPSTR(pgm_hostname)] = WiFi.getHostname();

    int wifiStatus = WiFi.status();

    if (wifiStatus == 255)
      root[FPSTR(pgm_status)] = "NO_SHIELD";
    else if (wifiStatus <= 6)
      root[FPSTR(pgm_status)] = FPSTR(wifistatus_P[wifiStatus]);

    root[FPSTR(pgm_mode)] = FPSTR(wifimode_P[WiFi.getMode()]);
    // const char *phymodes[] = {"", "B", "G", "N"};
    // root[FPSTR(pgm_phymode)] = phymodes[WiFi.getPhyMode()];
    root[FPSTR(pgm_channel)] = WiFi.channel();
    if (strcmp(_config.mode, "STA") == 0)
      root[FPSTR(pgm_ssid)] = WiFi.SSID();
    else if (strcmp(_config.mode, "AP") == 0 || strcmp(_config.mode, "AP_STA") == 0)
      root[FPSTR(pgm_ssid)] = WiFi.softAPgetHostname();
    root[FPSTR(pgm_password)] = WiFi.psk();
    root[FPSTR(pgm_encryption)] = WiFi.encryptionType(0);
    root[FPSTR(pgm_isconnected)] = WiFi.isConnected();
    root[FPSTR(pgm_autoconnect)] = WiFi.getAutoConnect();

    // root[FPSTR(pgm_persistent)] = WiFi.getPersistent();
    root[FPSTR(pgm_bssid)] = WiFi.BSSIDstr();
    root[FPSTR(pgm_rssi)] = WiFi.RSSI();
    root[FPSTR(pgm_sta_ip)] = WiFi.localIP().toString();
    root[FPSTR(pgm_sta_mac)] = WiFi.macAddress();
    root[FPSTR(pgm_ap_ip)] = WiFi.softAPIP().toString();
    root[FPSTR(pgm_ap_mac)] = WiFi.softAPmacAddress();
    root[FPSTR(pgm_gateway)] = WiFi.gatewayIP().toString();
    root[FPSTR(pgm_netmask)] = WiFi.subnetMask().toString();
    root[FPSTR(pgm_dns0)] = WiFi.dnsIP().toString();
    root[FPSTR(pgm_dns1)] = WiFi.dnsIP(1).toString();

    serializeJson(root, *response);
    request->send(response);
  });

  server.on("/config/network", [](AsyncWebServerRequest *request) {
    DEBUGLOG("%s\r\n", request->url().c_str());

    // send_config_network(request);

    DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    // DynamicJsonDocument root(2048);
    StaticJsonDocument<1024> root;

    root[FPSTR(pgm_mode)] = _config.mode;
    root[FPSTR(pgm_hostname)] = _config.hostname;
    root[FPSTR(pgm_ssid)] = _config.ssid;
    root[FPSTR(pgm_password)] = _config.password;
    root[FPSTR(pgm_dhcp)] = _config.dhcp;
    root[FPSTR(pgm_static_ip)] = _config.static_ip;
    root[FPSTR(pgm_netmask)] = _config.netmask;
    root[FPSTR(pgm_gateway)] = _config.gateway;
    root[FPSTR(pgm_dns0)] = _config.dns0;
    root[FPSTR(pgm_dns1)] = _config.dns1;

    serializeJsonPretty(root, *response);

    request->send(response);
  });

  server.on("/confignetwork", [](AsyncWebServerRequest *request) {
    DEBUGLOG("%s\r\n", request->url().c_str());

    //List all parameters
    int params = request->params();
    if (params)
    {
      for (int i = 0; i < params; i++)
      {
        AsyncWebParameter *p = request->getParam(i);
        if (p->isFile())
        { //p->isPost() is also true
          DEBUGLOG("FILE[%s]: %s, size: %u\r\n", p->name().c_str(), p->value().c_str(), p->size());
        }
        else if (p->isPost())
        {
          DEBUGLOG("POST [%s]: %s\r\n", p->name().c_str(), p->value().c_str());

          if (request->hasParam(FPSTR(pgm_saveconfig), true))
          {
            const char *config = p->value().c_str();

            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, config);

            File file = SPIFFS.open(FPSTR(pgm_configfilenetwork), "w");

            if (!file || error)
            {
              DEBUGLOG("Failed to open NETWORK config file\r\n");
              file.close();
              return;
            }

            serializeJsonPretty(doc, file);
            file.flush();
            file.close();

            load_config_network();

            // configFileNetworkUpdatedFlag = true;
          }
        }
        else
        {
          DEBUGLOG("GET[%s]: %s\r\n", p->name().c_str(), p->value().c_str());
        }
      }
    }
    else
    {
      // _httpAuth.auth = false;
    }

    // request->send(SPIFFS, request->url());
    request->send(SPIFFS, FPSTR(pgm_configpagenetwork));
  });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    DEBUGLOG("%s\r\n", request->url().c_str());

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument doc(2048);
    // StaticJsonDocument<1024> root;
    // JsonArray root = doc.createArray();
    JsonArray root = doc.to<JsonArray>();

    int numberOfNetworks = WiFi.scanComplete();
    if (numberOfNetworks == -2)
    {                          //this may also works: WiFi.scanComplete() == WIFI_SCAN_FAILED
      WiFi.scanNetworks(true); //async enabled
    }
    else if (numberOfNetworks)
    {
      for (int i = 0; i < numberOfNetworks; ++i)
      {
        JsonObject wifi = root.createNestedObject();
        wifi["ssid"] = WiFi.SSID(i);
        wifi["rssi"] = WiFi.RSSI(i);
        wifi["bssid"] = WiFi.BSSIDstr(i);
        wifi["channel"] = WiFi.channel(i);
        wifi["secure"] = WiFi.encryptionType(i);
        // wifi["hidden"] = WiFi.isHidden(i) ? true : false;
      }
      WiFi.scanDelete();
      if (WiFi.scanComplete() == -2)
      { //this may also works: WiFi.scanComplete() == WIFI_SCAN_FAILED
        WiFi.scanNetworks(true);
      }
    }
    serializeJson(root, *response);
    request->send(response);
    // example: [{"ssid":"OpenWrt","rssi":-10,"bssid":"A2:F3:C1:FF:05:6A","channel":11,"secure":4,"hidden":false},{"ssid":"DIRECT-sQDESKTOP-7HDAOQDmsTR","rssi":-52,"bssid":"22:F3:C1:F8:B1:E9","channel":11,"secure":4,"hidden":false},{"ssid":"galaxi","rssi":-11,"bssid":"A0:F3:C1:FF:05:6A","channel":11,"secure":4,"hidden":false},{"ssid":"HUAWEI-4393","rssi":-82,"bssid":"D4:A1:48:3C:43:93","channel":11,"secure":4,"hidden":false}]
  });

  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);
    handleFileList(request);
  });

  server.on("/admin/restart", [](AsyncWebServerRequest *request) {
    DEBUGLOG("%s\r\n", request->url().c_str());
    restart_esp(request);
  });

  server.on("/reset", [](AsyncWebServerRequest *request) {
    DEBUGLOG("%s\r\n", request->url().c_str());
    restart_esp(request);
  });

  server.on("/admin/wwwauth", [](AsyncWebServerRequest *request) {
    send_wwwauth_configuration_values_html(request);
  });

  server.on("/system.html", [](AsyncWebServerRequest *request) {
    send_wwwauth_configuration_html(request);
  });

  server.on("/update/updatepossible", [](AsyncWebServerRequest *request) {
    send_update_firmware_values_html(request);
  });

  server.on("/setmd5", [](AsyncWebServerRequest *request) {
    setUpdateMD5(request);
  });

  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/update.html");
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    const char* responseContent;
    if (Update.hasError())
    {
      responseContent = PSTR("FAIL");
    }
    else
    {
       responseContent = PSTR("<META http-equiv=\"refresh\" content=\"15;URL=/update\">Update correct. Restarting...");
    }
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", responseContent);
    
    response->addHeader("Connection", "close");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response); },
            [](AsyncWebServerRequest *request, const String filename, size_t index, uint8_t *data, size_t len, bool final) {
              updateFirmware(request, filename, index, data, len, final);
            });

  server.begin();

  save_system_info();

  // Serial.setDebugOutput(true);
}

void AsyncServerloop()
{
  ArduinoOTA.handle();
  ws.cleanupClients();

  // dnsServer.processNextRequest();

  // if (wifiMode == WIFI_AP)
  // {
  //   dnsServer.processNextRequest();
  // }

  if (WiFi.status() == WL_CONNECTED)
  {
    if (sendFreeHeapStatusFlag)
    {
      sendFreeHeapStatusFlag = false;
      sendHeap(2);
    }
  }
}