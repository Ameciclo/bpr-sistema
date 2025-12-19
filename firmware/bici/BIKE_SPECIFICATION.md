# 🚲 BPR Bike Firmware Specification

## 📋 Overview

The bike firmware should implement a **WiFi scanner** that collects network data and uploads it to a central base via **BLE communication**. The system operates in cycles: scan WiFi → store locally → connect to base → upload data → repeat.

## 🎯 Core Requirements

### 1. **WiFi Scanning**
- Scan for WiFi networks every **5 minutes** (configurable)
- Store networks with RSSI > **-90 dBm** (configurable)
- Collect: `SSID`, `BSSID`, `RSSI`, `Channel`, `Timestamp`
- Buffer up to **100 records** locally (configurable)
- Use **LittleFS** for persistent storage

### 2. **BLE Communication**
- Connect to base with name pattern: `BPR Central*`
- Use service UUID: `12345678-1234-1234-1234-123456789abc`
- Data characteristic: `12345678-1234-1234-1234-123456789abd`
- Config characteristic: `12345678-1234-1234-1234-123456789abe`
- Auto-reconnect after base sync cycles

### 3. **Data Format**
```json
{
  "bike_id": "bpr-7a90a9",
  "battery": 3.85,
  "records": 15,
  "timestamp": 12345,
  "heap": 152024,
  "wifi_scans": [
    {
      "ssid": "NET_5G",
      "bssid": "AA:BB:CC:11:22:33",
      "rssi": -65,
      "channel": 6
    }
  ]
}
```

### 4. **Configuration Management**
- Receive dynamic config from base via BLE notifications
- Store config in `/config.json` on LittleFS
- Support hot-reload of parameters
- Generate unique bike ID: `bpr-{6-char-hex}` from chip MAC

### 5. **Power Management**
- Monitor battery voltage via ADC
- Enter deep sleep when battery < **3.2V** (critical)
- Reduce scan frequency when battery < **3.45V** (low)
- USB power detection for development mode

## 🔧 Technical Implementation

### **State Machine**
```
BOOT → CONFIG_REQUEST → SCANNING → AT_BASE → SLEEP
  ↑                        ↑           ↓
  └────────────────────────┴───────────┘
```

### **Key Functions Required**

#### **WiFi Scanner**
```cpp
struct WiFiRecord {
    uint32_t timestamp;
    char ssid[33];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
};

int performWiFiScan();
void addWiFiRecord(const WiFiRecord& record);
void clearWiFiBuffer();
```

#### **BLE Client**
```cpp
bool scanForBase();
bool connectToBase(NimBLEAdvertisedDevice* device);
void sendStatus();
void sendWiFiData();
void onConfigReceived(const String& config);
```

#### **Configuration**
```cpp
struct Config {
    char bike_id[32];
    char bike_name[32];
    int scan_interval_sec;
    int wifi_max_networks;
    int wifi_rssi_threshold;
    float battery_critical_voltage;
    float min_battery_voltage;
    bool dev_mode;
};

bool loadConfig();
void saveConfig();
void processConfigUpdate(const String& json);
```

#### **Power Management**
```cpp
float getBatteryVoltage();
bool isBatteryCritical();
bool isBatteryLow();
void enterDeepSleep(int seconds);
```

## 📡 Communication Protocol

### **Data Upload (Bike → Base)**
1. Connect to `BPR Central*` via BLE
2. Send status heartbeat every 5 seconds
3. Upload WiFi scan data when available
4. Wait for config updates
5. Disconnect when base enters sync mode

### **Config Download (Base → Bike)**
1. Listen for notifications on config characteristic
2. Parse JSON config with target bike validation
3. Update local config and save to LittleFS
4. Send confirmation back to base

### **Expected Config Format**
```json
{
  "type": "config_push",
  "bike_id": "bpr-7a90a9",
  "config": {
    "version": 2,
    "bike_name": "Bike Intenso",
    "dev_mode": false,
    "wifi": {
      "scan_interval_sec": 300,
      "scan_timeout_ms": 5000,
      "max_networks": 20,
      "rssi_threshold": -90
    },
    "ble": {
      "base_name": "BPR Central",
      "scan_time_sec": 5,
      "connection_timeout_ms": 10000
    },
    "power": {
      "deep_sleep_duration_sec": 3600,
      "radio_coordination_delay_ms": 300
    },
    "battery": {
      "critical_voltage": 3.2,
      "low_voltage": 3.45,
      "full_voltage": 4.2
    }
  }
}
```

## 🚨 Error Handling

### **Connection Issues**
- Retry BLE connection up to 3 times
- Fall back to scanning mode if base not found
- Auto-reconnect after base sync cycles (90 seconds)

### **Memory Management**
- Clear WiFi buffer after successful upload
- Implement circular buffer if storage full
- Monitor heap usage and log warnings

### **Battery Protection**
- Force deep sleep if voltage < 3.2V
- Reduce scan frequency if voltage < 3.45V
- Skip BLE operations in critical battery state

### **Config Validation**
- Validate JSON structure before applying
- Use fallback defaults for missing fields
- Log config errors but continue operation

## 🔍 Debugging Features

### **Serial Output**
- Structured logging with emojis: `🔋 Battery: 3.85V`
- State transitions: `🔄 SCANNING → AT_BASE`
- Error messages: `❌ BLE connection failed`
- Data uploads: `📤 WiFi data: 15 records sent`

### **Development Mode**
- Enable via `dev_mode: true` in config
- Ignore battery protection (allow USB power)
- Verbose logging of all operations
- Shorter timeouts for faster testing

## 📊 Performance Targets

- **Scan Cycle**: 5 minutes (configurable)
- **BLE Connection**: < 10 seconds
- **Data Upload**: < 30 seconds
- **Battery Life**: > 7 days continuous operation
- **Memory Usage**: < 200KB heap, < 50KB storage
- **WiFi Networks**: Up to 20 per scan

## 🔧 Hardware Requirements

- **ESP32** (any variant with WiFi + BLE)
- **Battery monitoring** via ADC pin
- **Status LED** (optional)
- **Configuration button** (optional)
- **LittleFS** partition for storage

## 🚀 Success Criteria

1. ✅ **Stable WiFi scanning** every 5 minutes
2. ✅ **Reliable BLE connection** to base
3. ✅ **Successful data upload** with JSON format
4. ✅ **Dynamic configuration** from base
5. ✅ **Battery protection** and power management
6. ✅ **Persistent storage** of scans and config
7. ✅ **Auto-recovery** from connection failures
8. ✅ **Development mode** for testing

## 📝 Current Issues to Fix

### **Config Reception Problem**
```
📥 Config received: ?
❌ Invalid config JSON
```
**Root Cause**: BLE characteristic receiving corrupted data
**Solution**: Add data validation and error handling in config callback

### **Recommended Fixes**
1. **Validate config data length** before parsing
2. **Add CRC check** for config integrity
3. **Implement retry mechanism** for failed config updates
4. **Add config version tracking** to avoid duplicates
5. **Improve error logging** with hex dump of received data

---

**This specification defines a robust, configurable WiFi scanning bike that communicates reliably with the central base station.**