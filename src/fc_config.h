#ifndef _FC_CONFIG_H_
#define _FC_CONFIG_H_

//Home WiFi Settings
#define STA_SSID NULL //NULL means that it will not connect to WiFi
#define STA_PASSWORD NULL //NULL means that WiFi does not require password
//#define STA_SSID "my-home-wifi"
//#define STA_PASSWORD "big-secret"

//WiFi AP Settings
#define AP_SSID "esp8266fcgui"
#define AP_PASSWORD NULL //NULL means that AP does not require password


typedef struct {
  char mode[7] = "AP";
  char hostname[32] = "ESP_XXXX";
  char ssid[33] = "your_wifi_ssid";
  char password[65] = "your_wifi_password";
  bool dhcp = true;
  char static_ip[16] = "192.168.10.15";
  char netmask[16] = "255.255.255.0";
  char gateway[16] = "192.168.10.1";
  char dns0[16] = "192.168.10.1";
  char dns1[16] = "8.8.8.8";
} strConfig;
extern strConfig _config; // General and WiFi configuration

typedef struct
{
  const char* ssid = _config.hostname; // ChipID is appended to this name
  char password[10] = ""; // NULL means no password
  bool APenable = false; // AP disabled by default
} strApConfig;
extern strApConfig _configAP; // Static AP config settings

typedef struct {
  bool auth;
  char wwwUsername[32];
  char wwwPassword[32];
} strHTTPAuth;
extern strHTTPAuth _httpAuth;

#endif
