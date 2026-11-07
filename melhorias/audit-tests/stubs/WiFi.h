#pragma once
#include "Arduino.h"
#define WIFI_STA 1
#define WIFI_AP 2
#define WIFI_OFF 0
#define WL_CONNECTED 3
inline int mockScans=0;
inline int mockWifiStatus=WL_CONNECTED;
inline int mockNetworkCount=3;
struct IPAddress{IPAddress(int=0,int=0,int=0,int=0){}String toString(){return "127.0.0.1";}};
struct MockWiFi {
 int getMode(){return WIFI_STA;}void mode(int){} void begin(const char*,const char*){}void softAPConfig(IPAddress,IPAddress,IPAddress){}bool softAP(const char*,const char*){return true;}IPAddress softAPIP(){return {};}void softAPdisconnect(bool){}int status(){return mockWifiStatus;}void disconnect(bool){}
 int scanNetworks(){mockScans++;return mockNetworkCount;}
 int RSSI(int){return -60;} int channel(int){return 6;}
 String SSID(int i){return String("wifi-")+String(i);}
 String BSSIDstr(int){return "AA:BB:CC:DD:EE:FF";}
 void scanDelete(){} IPAddress localIP(){return {};}
};
inline MockWiFi WiFi;
