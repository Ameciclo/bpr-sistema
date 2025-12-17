# 🚲 Protocolo BLE para Bicicletas - BPR Hub

Documentação técnica para desenvolvimento de firmware de bicicletas compatível com o BPR Hub.

## 📋 Requisitos Obrigatórios

### 🔵 Configuração BLE

**Nome do dispositivo:**
- Formato: `bpr-XXXXXX` (exatamente 10 caracteres)
- Exemplo: `bpr-7a90a9`, `bpr-abc123`
- ❌ Inválido: `bike01`, `bpr_test`, `bpr-toolong`

**Modo de operação:**
- Bicicleta = **Cliente BLE** (escaneia e conecta)
- Hub = **Servidor BLE** (aguarda conexões)

### 🔍 Descoberta do Hub

**Escanear por:**
- Nome do serviço: `BPR Hub Station`
- Service UUID: `12345678-1234-1234-1234-123456789abc`

**Exemplo de código:**
```cpp
// Escanear por hubs BPR
NimBLEScan* pScan = NimBLEDevice::getScan();
NimBLEScanResults results = pScan->start(10);

for (int i = 0; i < results.getCount(); i++) {
    NimBLEAdvertisedDevice device = results.getDevice(i);
    if (device.getName() == "BPR Hub Station") {
        // Hub encontrado - conectar
        connectToHub(device.getAddress());
        break;
    }
}
```

## 📡 Características BLE

### 📤 Data Characteristic (Envio de Dados)
- **UUID:** `87654321-4321-4321-4321-cba987654321`
- **Propriedades:** READ | WRITE
- **Uso:** Enviar dados de status e scans WiFi

### ⚙️ Config Characteristic (Configurações)
- **UUID:** `11111111-2222-3333-4444-555555555555`
- **Propriedades:** READ | WRITE | NOTIFY
- **Uso:** Receber configurações do hub

## 📋 Formato de Dados JSON

### 📤 Dados de Status (Obrigatório)

```json
{
  "bike_id": "bpr-7a90a9",
  "battery": 85,
  "records": 0,
  "timestamp": 12345,
  "heap": 45000
}
```

**Campos obrigatórios:**
- `bike_id` (string): ID único da bicicleta no formato bpr-XXXXXX
- `battery` (number): Nível de bateria (0-100 ou voltagem)
- `records` (number): Número de registros WiFi coletados
- `timestamp` (number): Timestamp atual da bicicleta

**Campos opcionais:**
- `heap` (number): Memória livre disponível
- `mode` (string): Modo de operação atual
- `wifi_scans` (array): Dados de scan WiFi (ver formato abaixo)

### 📡 Dados de Scan WiFi (Opcional)

```json
{
  "bike_id": "bpr-7a90a9",
  "battery": 85,
  "records": 5,
  "timestamp": 12345,
  "wifi_scans": [
    {
      "ssid": "NET_5G_HOME",
      "bssid": "AA:BB:CC:DD:EE:FF",
      "rssi": -65,
      "channel": 6
    },
    {
      "ssid": "CLARO_WIFI",
      "bssid": "11:22:33:44:55:66", 
      "rssi": -78,
      "channel": 11
    }
  ]
}
```

## 🔄 Fluxo de Comunicação

### 1️⃣ Conexão Inicial

```cpp
// 1. Escanear e encontrar hub
// 2. Conectar ao hub
NimBLEClient* pClient = NimBLEDevice::createClient();
pClient->connect(hubAddress);

// 3. Obter service e characteristics
NimBLERemoteService* pService = pClient->getService("12345678-1234-1234-1234-123456789abc");
NimBLERemoteCharacteristic* pDataChar = pService->getCharacteristic("87654321-4321-4321-4321-cba987654321");
NimBLERemoteCharacteristic* pConfigChar = pService->getCharacteristic("11111111-2222-3333-4444-555555555555");
```

### 2️⃣ Envio de Dados

```cpp
// Enviar dados a cada 5 segundos
void sendStatus() {
    DynamicJsonDocument doc(512);
    doc["bike_id"] = "bpr-7a90a9";
    doc["battery"] = getBatteryLevel();
    doc["records"] = getWiFiRecordCount();
    doc["timestamp"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    pDataChar->writeValue(jsonString.c_str());
}
```

### 3️⃣ Recebimento de Configurações

```cpp
// Callback para receber configs do hub
class ConfigCallbacks : public NimBLECharacteristicCallbacks {
    void onNotify(NimBLERemoteCharacteristic* pChar) {
        std::string value = pChar->getValue();
        
        DynamicJsonDocument doc(512);
        deserializeJson(doc, value.c_str());
        
        if (doc["type"] == "config_update") {
            // Aplicar nova configuração
            int scanInterval = doc["wifi_scan_interval_sec"];
            updateScanInterval(scanInterval);
            
            // Confirmar recebimento
            sendConfigConfirmation("received");
        }
    }
};
```

## 🔐 Sistema de Aprovação

### 📝 Status da Bicicleta

**🟡 PENDING (Inicial):**
- Bike nova, primeira conexão
- Pode conectar no BLE
- ❌ Dados são ignorados pelo hub
- Aguarda aprovação do administrador

**✅ ALLOWED (Aprovada):**
- Bike aprovada pelo admin
- Pode conectar e enviar dados
- ✅ Dados são processados e enviados ao Firebase

**🚫 BLOCKED (Bloqueada):**
- Bike rejeitada pelo admin
- ❌ Conexão BLE é rejeitada
- Não pode enviar dados

### 🔄 Processo de Aprovação

1. **Primeira conexão:** Bike vira PENDING automaticamente
2. **Admin aprova:** Via dashboard ou Firebase
3. **Próximo sync:** Hub baixa aprovação
4. **Bike aprovada:** Pode enviar dados normalmente

## ⚠️ Comportamentos Esperados

### 🔄 Reconexão Automática

```cpp
void loop() {
    if (!pClient->isConnected()) {
        Serial.println("🔄 Reconectando ao hub...");
        scanAndConnect();
    }
    
    // Enviar dados a cada 5 segundos
    if (millis() - lastDataSent > 5000) {
        sendStatus();
        lastDataSent = millis();
    }
    
    delay(1000);
}
```

### 📡 Gerenciamento de Desconexão

- **Hub entra em WiFi sync:** BLE desliga temporariamente
- **Bike perde conexão:** Deve tentar reconectar automaticamente
- **Timeout:** Se não reconectar em 30s, voltar ao modo scan

### 🔋 Economia de Energia

```cpp
// Reduzir frequência quando bateria baixa
int getDataInterval() {
    int battery = getBatteryLevel();
    if (battery < 20) {
        return 30000; // 30 segundos
    } else {
        return 5000;  // 5 segundos
    }
}
```

## 🧪 Exemplo Completo

```cpp
#include <NimBLEDevice.h>
#include <ArduinoJson.h>

class BikeClient {
private:
    NimBLEClient* pClient = nullptr;
    NimBLERemoteCharacteristic* pDataChar = nullptr;
    NimBLERemoteCharacteristic* pConfigChar = nullptr;
    String bikeId = "bpr-7a90a9";
    
public:
    bool scanAndConnect() {
        NimBLEScan* pScan = NimBLEDevice::getScan();
        NimBLEScanResults results = pScan->start(10);
        
        for (int i = 0; i < results.getCount(); i++) {
            NimBLEAdvertisedDevice device = results.getDevice(i);
            if (device.getName() == "BPR Hub Station") {
                return connectToHub(device.getAddress());
            }
        }
        return false;
    }
    
    bool connectToHub(NimBLEAddress address) {
        pClient = NimBLEDevice::createClient();
        
        if (!pClient->connect(address)) {
            return false;
        }
        
        NimBLERemoteService* pService = pClient->getService("12345678-1234-1234-1234-123456789abc");
        if (!pService) return false;
        
        pDataChar = pService->getCharacteristic("87654321-4321-4321-4321-cba987654321");
        pConfigChar = pService->getCharacteristic("11111111-2222-3333-4444-555555555555");
        
        if (pConfigChar) {
            pConfigChar->subscribe(true, [this](NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
                handleConfigUpdate(std::string((char*)pData, length));
            });
        }
        
        return (pDataChar != nullptr);
    }
    
    void sendStatus() {
        if (!pDataChar) return;
        
        DynamicJsonDocument doc(512);
        doc["bike_id"] = bikeId;
        doc["battery"] = getBatteryLevel();
        doc["records"] = 0;
        doc["timestamp"] = millis() / 1000;
        doc["heap"] = ESP.getFreeHeap();
        
        String jsonString;
        serializeJson(doc, jsonString);
        
        pDataChar->writeValue(jsonString.c_str());
        Serial.printf("📤 Status: %s\n", jsonString.c_str());
    }
    
    void handleConfigUpdate(const std::string& config) {
        Serial.printf("📥 Config: %s\n", config.c_str());
        // Processar configuração recebida
    }
    
    int getBatteryLevel() {
        // Implementar leitura real da bateria
        return 85;
    }
};

BikeClient bike;

void setup() {
    Serial.begin(115200);
    NimBLEDevice::init("bpr-7a90a9");
    
    Serial.println("🚲 Iniciando cliente BLE...");
    if (bike.scanAndConnect()) {
        Serial.println("✅ Conectado ao hub!");
    }
}

void loop() {
    static uint32_t lastSent = 0;
    
    if (millis() - lastSent > 5000) {
        bike.sendStatus();
        lastSent = millis();
    }
    
    delay(1000);
}
```

## 🔧 Troubleshooting

### ❌ Bike não conecta
- Verificar nome do dispositivo (formato `bpr-XXXXXX`)
- Confirmar UUIDs das características
- Hub pode estar em modo WiFi sync

### ❌ Dados não aparecem no Firebase
- Bike pode estar com status PENDING
- Verificar formato JSON dos dados
- Confirmar campo `bike_id` obrigatório

### ❌ Desconexões frequentes
- Implementar reconexão automática
- Verificar interferência WiFi/BLE
- Ajustar intervalo de envio de dados

## 📚 Referências

- **Hub firmware:** `/firmware/hub/src/ble_only.cpp`
- **Configurações:** `/firmware/hub/include/constants.h`
- **Firebase structure:** Documentação do projeto BPR