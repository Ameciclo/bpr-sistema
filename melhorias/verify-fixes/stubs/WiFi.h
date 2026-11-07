#pragma once
// WiFi com scan assíncrono conforme a API do Arduino-ESP32:
// scanNetworks(async=true,...) retorna WIFI_SCAN_RUNNING (-1) e o resultado
// fica disponível depois via scanComplete().
#include "Arduino.h"
#include <queue>

#define WIFI_STA 1
#define WIFI_AP 2
#define WIFI_OFF 0
#define WL_CONNECTED 3
#define WIFI_SCAN_RUNNING (-1)
#define WIFI_SCAN_FAILED (-2)

inline int mockScans = 0;
inline int mockWifiStatus = WL_CONNECTED;
inline int mockNetworkCount = 3;
inline int mockPollsUntilComplete = 2; // scanComplete() retorna RUNNING N vezes
inline bool mockAsyncScan = true;
inline int mockScanResult = -999;      // resultado final do scan assíncrono
inline bool mockScanInFlight = false;
inline int mockScanStarts = 0;

inline String mockSsid(int i) { return String("wifi-") + String(i); }

struct IPAddress { IPAddress(int = 0, int = 0, int = 0, int = 0) {} String toString() { return "127.0.0.1"; } };

struct MockWiFi {
  int getMode() { return WIFI_STA; }
  void mode(int) {}
  void begin(const char*, const char*) {}
  void softAPConfig(IPAddress, IPAddress, IPAddress) {}
  bool softAP(const char*, const char*) { return true; }
  IPAddress softAPIP() { return {}; }
  void softAPdisconnect(bool) {}
  int status() { return mockWifiStatus; }
  void disconnect(bool) {}

  // Assinatura usada pelo firmware: (async, show_hidden, passive, max_ms_per_chan)
  int scanNetworks(bool async, bool = true, bool = false, int = 5000) {
    mockScans++;
    if (async) {
      mockScanStarts++;
      mockScanInFlight = true;
      mockPollsUntilComplete = 2;
      return WIFI_SCAN_RUNNING;
    }
    return mockNetworkCount;
  }

  int scanComplete() {
    if (!mockScanInFlight) return mockScanResult == -999 ? mockNetworkCount : mockScanResult;
    if (mockPollsUntilComplete > 0) { mockPollsUntilComplete--; return WIFI_SCAN_RUNNING; }
    mockScanInFlight = false;
    return mockNetworkCount;
  }

  int RSSI(int) { return -60; }
  int channel(int) { return 6; }
  String SSID(int i) { return mockSsid(i); }
  String BSSIDstr(int) { return "AA:BB:CC:DD:EE:FF"; }
  void scanDelete() {}
  IPAddress localIP() { return {}; }
};
inline MockWiFi WiFi;
