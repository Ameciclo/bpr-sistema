# 🔗 BPR Common Firmware Files

This directory contains **minimal shared definitions** for BLE communication between **bike** and **central** firmware.

## 📁 Files Overview

### `bpr_protocol.h` - BLE Protocol Only
- **BLE UUIDs** and service definitions
- **Message types** for BLE communication
- **Data limits** for BLE packet sizes
- **Device naming** constants

### `bpr_types.h` - Essential Structures
- **WiFiRecord** - Network scan data format
- **Message structs** - BLE communication formats
- **ID utilities** - Bike ID generation/validation
- **BLE helpers** - BSSID formatting, validation

## 🎯 Usage

```cpp
#include "../common/bpr_protocol.h"
#include "../common/bpr_types.h"

// Use shared BLE constants
NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

// Use shared structures
WiFiRecord wifiBuffer[MAX_WIFI_NETWORKS_BLE];
StatusMessage status;

// Use shared utilities
String bikeId = generateBikeId();
```

## 🔧 What's NOT Here

- **Configuration values** (come from Firebase)
- **Timing constants** (device-specific)
- **Battery thresholds** (configurable)
- **State machines** (different for bike/central)
- **Complex utilities** (keep in respective firmwares)

## 📋 Key Constants

| Constant | Value | Purpose |
|----------|-------|----------|
| `BLE_SERVICE_UUID` | `12345678-1234-1234-1234-123456789abc` | Main BLE service |
| `BLE_CHAR_DATA_UUID` | `12345678-1234-1234-1234-123456789abd` | Data transfer |
| `BLE_CHAR_CONFIG_UUID` | `12345678-1234-1234-1234-123456789abe` | Configuration |
| `CENTRAL_BLE_NAME` | `BPR Central` | Base station name |
| `BIKE_ID_PREFIX` | `bpr-` | Bike ID format |
| `MAX_BLE_PACKET_SIZE` | `512` | Safe BLE packet size |

---

**These files ensure BLE protocol compatibility without duplicating configurable values.**