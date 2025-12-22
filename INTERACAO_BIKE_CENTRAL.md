# Interação Bicicleta ↔ Central
## 🚲 Sistema de Bicicletas Compartilhadas Comunitárias

### 🏠 **Cenário Principal: Bikes na Base (90% do tempo)**
- **10 bicicletas** ficam "dormindo" na central
- **Acordam a cada 5min** para "dar oi" via BLE
- **Verificam configs** (mudam mensalmente)
- **Reportam bateria** para monitoramento
- **Voltam a dormir** para economizar energia

### 🚴 **Cenário Secundário: Bikes em Uso (10% do tempo)**
- **Saem da base** → Central percebe (parou de "dar oi")
- **Coletam WiFi** durante o passeio
- **Voltam com dados** → Upload sem pressa na fila BLE
- **Recebem confirmação** → Limpam buffer → Voltam a dormir

## 📡 **Conexão BLE Detalhada**

### 🔍 **Descoberta e Conexão**

**Bicicleta (Cliente BLE):**
1. Faz scan BLE procurando nome `"BPR Central*"`
2. Conecta no primeiro dispositivo encontrado
3. Busca serviço UUID específico
4. Obtém 2 características: DATA + CONFIG
5. Se inscreve para notificações CONFIG

**Central (Servidor BLE):**
1. Sempre anunciando como `"BPR Central"`
2. Aceita múltiplas conexões simultâneas
3. Mapeia cada conexão por `handle` → `bike_id`
4. Processa dados via callbacks

### 📋 **Características BLE**

| Canal | Direção | Função | Propriedades |
|-------|---------|--------|--------------|
| **DATA** | Bike → Central | Scans WiFi, status, bateria | READ + WRITE |
| **CONFIG** | Central → Bike | Configurações dinâmicas | READ + WRITE + NOTIFY |

### 🔄 **Fluxo "DAR OI" (Bikes na Base)**

```
🚲 BIKE (a cada 5min)              🏢 CENTRAL
  │                                   │
  ├─ 1. Acorda do deep sleep          │
  ├─ 2. BLE Scan rápido (10s)         │
  │   ┌─ Advertising: "AVAILABLE"  ────┤
  ├─ 3. Conecta "BPR Central"     ────┤
  │                               ├─ 4. Aceita + timestamp NTP
  ├─ 5. "Oi! Bateria: 85%"        ────┤
  │                               ├─ 6. "Oi! Config v2 (se nova)"
  ├─ 7. Recebe config (se houver) ────┤
  ├─ 8. "Tchau!" + desconecta     ────┤
  │                               ├─ 9. Anota presença + bateria
  ├─ 10. Volta a dormir (4min50s)     │
```

### 🚴 **Fluxo "TENHO DADOS" (Bikes voltando)**

```
🚲 BIKE (com dados WiFi)           🏢 CENTRAL
  │                                   │
  ├─ 1. Acorda e detecta base         │
  ├─ 2. "Oi! Tenho 47 scans WiFi"────┤
  │                               ├─ 3. "Manda! Session: abc123"
  ├─ 4. Upload dados (sem pressa) ────┤
  │                               ├─ 5. Processa + salva
  ├─ 6. Aguarda confirmação...    ────┤
  │                               ├─ 7. "ACK! Pode limpar buffer"
  ├─ 8. Limpa dados locais        ────┤
  ├─ 9. Volta modo "dar oi"       ────┤
```

### 🚫 **Fluxo "CENTRAL OCUPADA"**

```
🚲 BIKE                            🏢 CENTRAL (WiFi Sync)
  │                                   │
  ├─ 1. Tenta "dar oi"                │
  │   ┌─ Advertising: "BUSY:2min"  ────┤
  ├─ 2. Lê "ocupada por 2min"     ────┤
  ├─ 3. Dorme +1min extra (config)    │
  ├─ 4. Retry após 3min total         │
  │   ┌─ Advertising: "AVAILABLE"  ────┤
  ├─ 5. Conecta normalmente...    ────┤
```

### 📦 **Protocolo JSON ESPECÍFICO**

**1. "Dar Oi" - Bike → Central:**
```json
{
  "type": "heartbeat",
  "bike_id": "bpr-abc123",
  "battery_percent": 85,
  "has_data": false,
  "config_version": 2
}
```

**2. "Oi de Volta" - Central → Bike:**
```json
{
  "type": "heartbeat_ack",
  "ntp_timestamp": 1733459200,
  "config_update": null,  // ou objeto config se nova
  "next_checkin_sec": 300
}
```

**3. "Tenho Dados" - Bike → Central:**
```json
{
  "type": "data_upload",
  "bike_id": "bpr-abc123",
  "battery_percent": 78,
  "records_count": 47,
  "session_start": 1733458000,
  "wifi_scans": [...]
}
```

**4. "Pode Limpar" - Central → Bike:**
```json
{
  "type": "upload_confirmed",
  "session_id": "sess_abc123",
  "can_clear_buffer": true,
  "next_checkin_sec": 300
}
```

**5. BLE Advertising (Broadcast):**
```json
{
  "status": "AVAILABLE",     // ou "BUSY:120"
  "queue_size": 0,
  "estimated_wait": 0
}
```

### ⚡ **Timing Inteligente (Totalmente Configurável)**

**Todas as configurações vêm do Firebase:**

#### 🚲 **Bike Configs:**
```json
{
  "checkin_interval_sec": 300,        // "dar oi" a cada 5min
  "busy_retry_delay_sec": 60,         // +1min se central ocupada
  "max_checkin_attempts": 3,          // Máximo 3 tentativas
  "connection_timeout_ms": 10000,     // 10s timeout BLE
  "sleep_after_failed_attempts": 900  // 15min se falhar tudo
}
```

#### 🏢 **Central Configs:**
```json
{
  "sync_interval_sec": 240,           // Sync a cada 4min
  "wifi_timeout_sec": 60,             // Máximo 60s no WiFi
  "ble_queue_timeout_sec": 120,       // 2min máximo por bike
  "max_concurrent_bikes": 5,          // Fila máxima
  "advertising_update_interval_ms": 5000  // Atualiza status a cada 5s
}
```

#### 🔧 **Configuração Dinâmica por Base:**
```json
// Firebase: /bases/{base_id}/config
{
  "base_id": "ameciclo",
  "bike_defaults": {
    "checkin_interval_sec": 300,      // Padrão para bikes desta base
    "busy_retry_delay_sec": 60
  },
  "central_config": {
    "sync_interval_sec": 240,         // Específico desta central
    "wifi_timeout_sec": 45            // Ameciclo tem WiFi mais lento
  },
  "overrides": {
    "bpr-abc123": {                   // Config específica para uma bike
      "checkin_interval_sec": 180     // Esta bike "dá oi" a cada 3min
    }
  }
}
```

**Por que funciona:**
```
Tempo: 0  4  5  8  9  10 12 13 15 16 17 20
Sync:  |-----|     |-----|     |-----|  
Bike:     |     |     |     |     |     
       Livre  Oi  Livre Oi  Livre  Oi
```
*Mas agora TODOS os números são configuráveis!*

**Detecção de Problemas:**
- **Bot monitora** heartbeats no Firebase
- **Bike sumiu**: Não deu "oi" há >`max_silence_time` (configurável)
- **Bateria crítica**: <`battery_alert_percent` (configurável)
- **Problema técnico**: Bike presente mas não reporta

**Sistema de Fila:**
- **Timeout por bike**: `ble_queue_timeout_sec` (configurável)
- **Fila máxima**: `max_concurrent_bikes` (configurável)
- **Resto aguarda**: Com delay `busy_retry_delay_sec`

## ⚡ **Estados da Máquina OTIMIZADOS**

### 🚲 Bicicleta:
- `SLEEPING` (4min50s) → `WAKE_CHECK` (10s) → `AT_BASE` ou `SLEEPING`
- Se dados coletados: `WAKE_CHECK` → `DATA_UPLOAD` → `SLEEPING`

### 🏢 Central:
- `BIKE_PAIRING` (4min) ↔ `CLOUD_SYNC` (1min)
- **Números "primos entre si"**: 5min bikes vs 4min sync
- **BLE continua** durante WiFi sync com broadcast "BUSY"

## 🕐 **Sincronização de Tempo e Limpeza**

### ❌ **O que NÃO acontece atualmente:**

**Sobre Tempos:**
- ❌ Bike e Central **NÃO** sincronizam relógios
- ❌ Bike usa `millis()` (desde boot), Central usa `time()` (NTP)
- ❌ Timestamps ficam inconsistentes entre dispositivos
- ❌ Não há coordenação de "cochilos" (deep sleep)

**Sobre Limpeza de Dados:**
- ❌ Bike **NÃO** recebe confirmação de upload
- ❌ Central **NÃO** avisa "dados recebidos, pode apagar"
- ❌ Bike limpa buffer **imediatamente** após enviar
- ❌ Se falhar no meio, dados são perdidos para sempre

### 📊 **Dados Trocados Atualmente:**

**Bike → Central:**
```json
{
  "timestamp": 12345,     // millis() da bike (não confiável)
  "battery": 3.8,         // voltagem atual
  "records": 15,          // quantos scans no buffer
  "heap": 45000,          // memória livre
  "wifi_scans": [...]     // dados coletados
}
```

**Central → Bike:**
```json
{
  "config": {
    "scan_interval_sec": 300,      // quando fazer próximo scan
    "deep_sleep_sec": 3600,        // quanto tempo dormir
    "max_time_without_base_sec": 7200  // timeout para modo LOST
  }
}
```

### 🔧 **Melhorias Necessárias:**

1. **Sync de Tempo**: Central enviar timestamp NTP para bike
2. **Confirmação de Upload**: Central confirmar recebimento antes da bike limpar
3. **Coordenação de Sleep**: Central sugerir quando bike deve "cochilar"
4. **Retry Logic**: Bike manter dados até confirmação
5. **Heartbeat Sincronizado**: Timestamps consistentes

### 🎯 **Benefícios para Bicicletas Compartilhadas:**

✅ **Economia de Bateria** - Bikes dormem 98% do tempo
✅ **Monitoramento Contínuo** - Central sabe quem está presente
✅ **Detecção de Saída** - Bot avisa quando bike sai
✅ **Upload Confiável** - Dados preservados até confirmação
✅ **Configs Remotas** - Atualizações mensais sem acesso físico
✅ **Coexistência WiFi/BLE** - Sync não interrompe "dar oi"
✅ **Fila Organizada** - Múltiplas bikes voltando sem conflito
✅ **Alertas Inteligentes** - Bot detecta bateria baixa e problemas

---

*Sistema otimizado para o cenário real: "dorminhoco na base + aventureiro coletando dados"! 🚲💤*

## 🔧 **Mudanças de Código Necessárias**

### 🚲 **BICICLETA (firmware/bici/src/)**

#### **1. main.cpp - Estados Completos**
```cpp
// Estados da máquina de estados da bike
enum BikeState {
    STATE_SLEEPING,      // Deep sleep na base (4min50s)
    STATE_WAKE_CHECK,    // Acorda, verifica se está na base
    STATE_SCANNING,      // 🚴 NA RUA coletando WiFi scans
    STATE_DATA_UPLOAD,   // Voltou na base, upload dados
    STATE_LOW_BATTERY,   // Bateria crítica - modo emergência
    STATE_CONFIG_REQUEST // Para bikes SEM config válida (novas OU corrompidas)
};

// Timers vêm do config.json (carregados na inicialização)
// Valores padrão caso config.json não exista:
uint32_t checkin_interval_sec = 300;        // "dar oi" a cada 5min
uint32_t scan_interval_sec = 25;            // WiFi scan a cada 25s
uint32_t low_battery_scan_interval_sec = 120; // Scan mais lento se bateria baixa
uint8_t battery_critical_percent = 15;      // Entra em LOW_BATTERY
uint8_t battery_low_percent = 25;           // Reduz frequência de scans

// Fluxo de inicialização:
void setup() {
    if (!configManager.load() || !configManager.isValid()) {
        // Qualquer bike sem config válida (nova OU corrompida)
        currentState = STATE_CONFIG_REQUEST;
    } else {
        // Carregar timers do config.json
        loadTimersFromConfig();
        currentState = STATE_WAKE_CHECK;
    }
}
```

#### **2. at_base.cpp - Leitura de Advertising BUSY**
```cpp
// MODIFICAR: Retry inteligente com advertising check
bool AtBaseState::scanForBase() {
    Config& config = configManager.getConfig();
    Serial.printf("🔍 Scanning for BLE base '%s*' (timeout: %ds)...\n", 
                  config.base_ble_name, config.ble_scan_time_sec);
    
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    NimBLEScanResults results = pScan->start(config.ble_scan_time_sec, false);
    
    for (int i = 0; i < results.getCount(); i++) {
        NimBLEAdvertisedDevice device = results.getDevice(i);
        
        if (device.getName().find(config.base_ble_name) != std::string::npos) {
            Serial.printf("🔍 Found base: %s\n", device.getName().c_str());
            
            // Verificar manufacturer data para status
            std::string manufacturerData = device.getManufacturerData();
            String status = String(manufacturerData.c_str());
            
            if (status.startsWith("BUSY:")) {
                int waitTime = extractWaitTime(status);  // "BUSY:240" → 240s
                Serial.printf("🚫 Central busy for %ds, sleeping extra\n", waitTime);
                
                // Dormir tempo sugerido + margem de segurança
                uint32_t sleepTime = waitTime + config.busy_retry_delay_sec;
                powerManager.enterDeepSleep(sleepTime);
                
                pScan->clearResults();
                return false;  // Não conectar agora
            }
            
            // Status "AVAILABLE" ou sem status → conectar normalmente
            if (connectToBase(&device)) {
                pScan->clearResults();
                return true;
            }
        }
    }
    
    pScan->clearResults();
    Serial.println("❌ No BLE base found");
    return false;
}

// ADICIONAR: Função para extrair tempo do status BUSY
int AtBaseState::extractWaitTime(const String& status) {
    // "BUSY:240" → 240
    int colonPos = status.indexOf(':');
    if (colonPos > 0 && colonPos < status.length() - 1) {
        return status.substring(colonPos + 1).toInt();
    }
    return 60; // Default 1min se não conseguir parsear
}
```

#### **4. power_manager.cpp - ARQUIVO ÚNICO DE ENERGIA**
```cpp
// CRIAR: Gerenciamento completo de energia e bateria
class PowerManager {
public:
    // === MEDIÇÃO DE BATERIA ===
    uint8_t getBatteryPercent();           // Converte voltage para % (0-100)
    float getBatteryVoltage();             // Voltage bruto do ADC
    bool isBatteryCharging();              // Detecta se está carregando (USB)
    
    // === ESTADOS DE BATERIA ===
    BikeState checkBatteryState(BikeState currentState);  // Decide próximo estado
    bool isLowBattery();                   // < 25%
    bool isCriticalBattery();              // < 15%
    bool hasBatteryRecovered();            // > 30% (hysteresis)
    
    // === INTERVALOS DINÂMICOS (baseados na bateria) ===
    uint32_t getScanInterval();            // WiFi scan interval
    uint32_t getCheckinInterval();         // "Dar oi" interval
    uint32_t getSleepDuration();           // Deep sleep duration
    
    // === SLEEP MANAGEMENT ===
    void enterDeepSleep(uint32_t seconds);
    void scheduleWakeup(uint32_t base_interval, uint32_t extra_delay);
    
    // === LOW BATTERY BEHAVIOR ===
    BikeState handleLowBatteryState();     // Lógica específica do estado LOW_BATTERY
    bool tryEmergencyUpload();             // Upload urgente se na base
    void enterEmergencyMode();             // Configurações de emergência
    void exitEmergencyMode();              // Volta ao normal
    
    // === REPORTING ===
    void logBatteryStatus();               // Log detalhado da bateria
    String getBatteryReport();             // JSON para enviar à central
    
private:
    uint8_t lastBatteryPercent = 100;
    uint32_t lastBatteryRead = 0;
    bool emergencyMode = false;
    uint32_t emergencyModeStart = 0;
    
    void logBatteryTransition(BikeState from, BikeState to, uint8_t batteryPercent);
};

// === IMPLEMENTAÇÃO DOS COMPORTAMENTOS ===

// Decide próximo estado baseado na bateria
BikeState PowerManager::checkBatteryState(BikeState currentState) {
    uint8_t battery = getBatteryPercent();
    
    // Transição para LOW_BATTERY
    if (battery <= battery_critical_percent && currentState != STATE_LOW_BATTERY) {
        logBatteryTransition(currentState, STATE_LOW_BATTERY, battery);
        enterEmergencyMode();
        return STATE_LOW_BATTERY;
    }
    
    // Saída de LOW_BATTERY (hysteresis)
    if (currentState == STATE_LOW_BATTERY && battery >= battery_recovery_percent) {
        logBatteryTransition(STATE_LOW_BATTERY, STATE_WAKE_CHECK, battery);
        exitEmergencyMode();
        return STATE_WAKE_CHECK;
    }
    
    return currentState; // Sem mudança
}

// Lógica específica do estado LOW_BATTERY
BikeState PowerManager::handleLowBatteryState() {
    uint8_t battery = getBatteryPercent();
    
    // Se bateria melhorou, sair do modo
    if (battery >= battery_recovery_percent) {
        return STATE_WAKE_CHECK;
    }
    
    // Tentar upload de emergência se na base
    if (tryEmergencyUpload()) {
        // Upload feito, pode dormir mais tempo
        enterDeepSleep(deep_sleep_critical_sec);
        return STATE_LOW_BATTERY;
    }
    
    // Sem base, dormir muito tempo
    enterDeepSleep(deep_sleep_critical_sec);
    return STATE_LOW_BATTERY;
}

// Intervalos baseados na bateria atual
uint32_t PowerManager::getScanInterval() {
    uint8_t battery = getBatteryPercent();
    
    if (battery <= battery_critical_percent) {
        return scan_interval_critical_sec;     // 5min+ (economia extrema)
    } else if (battery <= battery_low_percent) {
        return scan_interval_low_battery_sec;  // 2min (economia)
    } else {
        return scan_interval_normal_sec;       // 25s (normal)
    }
}

uint32_t PowerManager::getCheckinInterval() {
    uint8_t battery = getBatteryPercent();
    
    if (battery <= battery_critical_percent) {
        return checkin_low_battery_sec * 2;    // 20min (muito esporádico)
    } else if (battery <= battery_low_percent) {
        return checkin_low_battery_sec;        // 10min (esporádico)
    } else {
        return checkin_interval_sec;           // 5min (normal)
    }
}

// Relatório da bateria para enviar à central
String PowerManager::getBatteryReport() {
    DynamicJsonDocument doc(256);
    doc["battery_percent"] = getBatteryPercent();
    doc["battery_voltage"] = getBatteryVoltage();
    doc["is_charging"] = isBatteryCharging();
    doc["emergency_mode"] = emergencyMode;
    doc["scan_interval_sec"] = getScanInterval();
    doc["checkin_interval_sec"] = getCheckinInterval();
    
    String result;
    serializeJson(doc, result);
    return result;
}
```

#### **6. config_manager.cpp - Configs do config.json**
```cpp
// MODIFICAR: Estrutura completa do config.json
struct Config {
    // Identificação
    char bike_id[32];
    char bike_name[64];
    uint32_t config_version;
    
    // Heartbeat configs (carregados do config.json)
    uint32_t checkin_interval_sec = 300;           // "dar oi" a cada 5min
    uint32_t checkin_low_battery_sec = 600;        // "dar oi" menos frequente se bateria baixa
    uint32_t busy_retry_delay_sec = 60;            // +1min se central ocupada
    
    // Battery thresholds (carregados do config.json)
    uint8_t battery_critical_percent = 15;         // Entra em LOW_BATTERY
    uint8_t battery_low_percent = 25;              // Reduz atividade
    uint8_t battery_recovery_percent = 30;         // Sai de LOW_BATTERY
    
    // Scan intervals baseados na bateria (carregados do config.json)
    uint32_t scan_interval_normal_sec = 25;        // Scan normal
    uint32_t scan_interval_low_battery_sec = 120;  // Scan economia
    uint32_t scan_interval_critical_sec = 300;     // Scan emergência
    
    // Sleep configs (carregados do config.json)
    uint32_t deep_sleep_normal_sec = 300;          // Sleep normal (5min)
    uint32_t deep_sleep_low_battery_sec = 1800;    // Sleep economia (30min)
    uint32_t deep_sleep_critical_sec = 3600;       // Sleep emergência (1h)
};

// ADICIONAR: Função para carregar timers
void loadTimersFromConfig() {
    Config& config = configManager.getConfig();
    
    // Atualizar variáveis globais com valores do config.json
    checkin_interval_sec = config.checkin_interval_sec;
    scan_interval_sec = config.scan_interval_normal_sec;
    low_battery_scan_interval_sec = config.scan_interval_low_battery_sec;
    battery_critical_percent = config.battery_critical_percent;
    battery_low_percent = config.battery_low_percent;
    
    Serial.printf("Timers loaded from config.json:\n");
    Serial.printf("  Checkin: %ds, Scan: %ds, Battery critical: %d%%\n", 
                  checkin_interval_sec, scan_interval_sec, battery_critical_percent);
}

// ADICIONAR: Validação de config
bool ConfigManager::isValid() {
    return (strlen(config.bike_id) > 0 && 
            config.config_version > 0 &&
            config.checkin_interval_sec > 0 &&
            config.scan_interval_normal_sec > 0);
}
```

#### **5. config_manager.cpp - URL de Presença**
```cpp
// ADICIONAR: URL para dados de presença
String ConfigManager::getPresenceDataUrl() const {
    return String(config.firebase.database_url) + 
           "/bases/" + config.base_id + "/presence.json?auth=" + 
           config.firebase.api_key;
}
```

#### **1. ble_server.cpp - Advertising com Status + Modo BUSY**
```cpp
// MODIFICAR: Advertising dinâmico + modo BUSY
class BPRBLEServer {
public:
    static void setAdvertisingOnly(String status);           // Só advertising, sem conexões
    static void updateAdvertisingData(String status);        // Atualizar status atual
    static void stopAcceptingConnections();                 // Parar aceitar conexões
    static void startAcceptingConnections();                // Voltar a aceitar conexões
};

// Implementação do modo BUSY
void BPRBLEServer::setAdvertisingOnly(String status) {
    // Parar aceitar novas conexões
    if (pServer) {
        pServer->getAdvertising()->stop();
    }
    
    // Configurar advertising com status
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    
    // Adicionar status no manufacturer data
    pAdvertising->setManufacturerData(0xFFFF, status);
    pAdvertising->setScanResponse(true);
    
    // Iniciar APENAS advertising (sem aceitar conexões)
    pAdvertising->start();
    
    Serial.printf("📡 Advertising only: %s\n", status.c_str());
}

void BPRBLEServer::stopAcceptingConnections() {
    // Desconectar bikes já conectadas
    if (pServer && connectedBikes > 0) {
        pServer->disconnect(0xFF);  // Disconnect all
        connectedBikes = 0;
        connectedDevices.clear();
        Serial.println("🚫 Stopped accepting BLE connections");
    }
}

void BPRBLEServer::startAcceptingConnections() {
    // Voltar a aceitar conexões normalmente
    if (pServer) {
        pServer->startAdvertising();
        Serial.println("✅ Started accepting BLE connections");
    }
}
```

#### **2. bike_pairing.cpp - Heartbeat vs Upload**
```cpp
// MODIFICAR: Diferenciar tipos de conexão
void BPRBLEServer::onBikeDataReceived(const String& bikeId, const String& jsonData) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, jsonData);
    
    String type = doc["type"];
    if (type == "heartbeat") {
        processHeartbeat(bikeId, doc);
    } else if (type == "data_upload") {
        processDataUpload(bikeId, doc);
    }
}

// ADICIONAR: Processamento de heartbeat
void BikePairing::processHeartbeat(const String& bikeId, JsonDocument& data) {
    // Atualizar presença no Firebase
    // Verificar se tem config nova
    // Responder rapidamente (não enfileirar)
    
    String response = generateHeartbeatResponse(bikeId);
    BPRBLEServer::pushConfigToBike(bikeId, response);
}

// ADICIONAR: Fila só para uploads
void BikePairing::processDataUpload(const String& bikeId, JsonDocument& data) {
    // Este vai para fila normal
    enqueueBike(bikeId, data.as<String>());
}
```

#### **4. cloud_sync.cpp - Upload de Presença**
```cpp
// ADICIONAR: Upload de dados de presença durante sync
bool CloudSync::uploadPresenceData() {
    DynamicJsonDocument doc(4096);
    
    // Early return se não há dados de presença
    if (!BikeManager::uploadPresenceToFirebase(doc)) {
        Serial.println("📝 No presence data to upload");
        return true; // Não ter dados não é erro
    }
    
    HTTPClient http;
    String url = configManager.getPresenceDataUrl(); // /bases/{base_id}/presence.json
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpCode = http.PUT(jsonString); // PUT substitui dados completos
    
    // Early return se falhar
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("❌ Presence upload failed: HTTP %d\n", httpCode);
        http.end();
        return false;
    }
    
    // Sucesso
    Serial.printf("📤 Presence data uploaded: %d bikes\n", doc.size());
    http.end();
    return true;
}

// MODIFICAR: Incluir upload de presença no sync
bool CloudSync::performSync() {
    bool success = true;
    
    // Sync normal
    success &= downloadCentralConfig();
    success &= downloadBikeData();
    success &= uploadBikeData();
    success &= uploadBufferData();
    success &= uploadHeartbeat();
    
    // ADICIONAR: Upload de presença
    success &= uploadPresenceData();
    
    return success;
}
```
```cpp
// MODIFICAR: Manter APENAS advertising durante sync
SyncResult CloudSync::enter() {
    Serial.println("📡 Entering CLOUD_SYNC mode");
    
    // Calcular tempo estimado de sync
    uint32_t estimatedSyncTime = configManager.getConfig().wifi_timeout_sec;
    
    // Manter APENAS advertising com status "BUSY"
    BPRBLEServer::setAdvertisingOnly("BUSY:" + String(estimatedSyncTime));
    
    // PARAR processamento de conexões BLE
    BPRBLEServer::stopAcceptingConnections();
    
    // Foco total no WiFi sync
    ledController.syncPattern();
    syncStartTime = millis();
    
    return SyncResult::IN_PROGRESS;
}

SyncResult CloudSync::update() {
    // Atualizar advertising com tempo restante
    uint32_t elapsed = (millis() - syncStartTime) / 1000;
    uint32_t remaining = configManager.getConfig().wifi_timeout_sec - elapsed;
    
    if (remaining > 0) {
        BPRBLEServer::updateAdvertisingData("BUSY:" + String(remaining));
    }
    
    // Fazer sync SEM processar BLE
    bool success = performSync();
    
    return success ? SyncResult::SUCCESS : SyncResult::FAILURE;
}

void CloudSync::exit() {
    // Voltar advertising para "AVAILABLE"
    BPRBLEServer::setAdvertisingOnly("AVAILABLE");
    
    // Reativar processamento de conexões BLE
    BPRBLEServer::startAcceptingConnections();
    
    WiFi.disconnect(true);
    Serial.println("🔚 Exiting CLOUD_SYNC mode");
}
```

#### **5. bike_manager.cpp - Heartbeat Tracking Local**
```cpp
// ADICIONAR: Rastreamento de heartbeats (salvo localmente)
struct BikePresence {
    String bike_id;
    uint32_t last_heartbeat;
    uint8_t battery_percent;
    bool has_pending_data;
    uint32_t config_version;
};

// MODIFICAR: Salvar heartbeats no LittleFS (NÃO direto no Firebase)
void BikeManager::updateHeartbeat(const String& bikeId, JsonDocument& heartbeat) {
    time_t now = time(nullptr);
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    
    char dateStr[64];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d %H:%M:%S UTC-3", &timeinfo);
    
    // Atualizar dados locais
    bikes[bikeId]["last_heartbeat"] = now;
    bikes[bikeId]["last_heartbeat_human"] = dateStr;
    bikes[bikeId]["battery_percent"] = heartbeat["battery_percent"];
    bikes[bikeId]["status"] = "present";
    bikes[bikeId]["has_pending_data"] = heartbeat["has_data"] | false;
    bikes[bikeId]["config_version"] = heartbeat["config_version"] | 1;
    
    // Salvar no LittleFS (não Firebase!)
    saveData(); // Salva bikes.json local
    
    Serial.printf("💓 Heartbeat local: %s (bat:%d%%, data:%s)\n", 
                  bikeId.c_str(), 
                  (int)heartbeat["battery_percent"],
                  heartbeat["has_data"] ? "yes" : "no");
}

// ADICIONAR: Upload de presença durante CLOUD_SYNC
bool BikeManager::uploadPresenceToFirebase(DynamicJsonDocument& doc) {
    if (!dataLoaded) return false;
    
    doc.clear();
    
    // Montar dados de presença para upload
    JsonObject obj = bikes.as<JsonObject>();
    for (JsonPair bike : obj) {
        String bikeId = bike.key().c_str();
        
        // Só incluir bikes que deram "oi" recentemente
        uint32_t lastHeartbeat = bike.value()["last_heartbeat"] | 0;
        if (lastHeartbeat > 0) {
            JsonObject presence = doc.createNestedObject(bikeId);
            presence["last_heartbeat"] = lastHeartbeat;
            presence["last_heartbeat_human"] = bike.value()["last_heartbeat_human"];
            presence["battery_percent"] = bike.value()["battery_percent"];
            presence["status"] = bike.value()["status"];
            presence["has_pending_data"] = bike.value()["has_pending_data"];
            presence["config_version"] = bike.value()["config_version"];
        }
    }
    
    Serial.printf("📤 Presence data ready: %d bikes\n", doc.size());
    return doc.size() > 0;
}
```

### 📁 **NOVOS ARQUIVOS NECESSÁRIOS**

#### **firmware/bici/src/power_manager.h/cpp**
- Gerenciamento de deep sleep inteligente
- Cálculo de battery percentage
- Wake-up scheduling com delays

#### **firmware/central/src/presence_tracker.h/cpp**
- Rastreamento de bikes presentes
- Detecção de saídas (timeout heartbeat)
- Interface com Firebase para bot

### ⚙️ **CONFIGURAÇÕES FIREBASE**

#### **Estrutura Nova:**
```json
{
  "bases": {
    "base01": {
      "presence": {
        "bpr-abc123": {
          "last_heartbeat": 1733459200,
          "battery_percent": 85,
          "status": "present",
          "has_data": false
        }
      },
      "config": {
        "checkin_interval_sec": 300,
        "sync_interval_sec": 240,
        "busy_retry_delay_sec": 60
      }
    }
  }
}
```

---

**Resumo das Mudanças:**
- 🚲 **Bike**: Estados simplificados, heartbeat protocol, power management
- 🏢 **Central**: Advertising contínuo, heartbeat vs upload, timing inteligente
- 📡 **BLE**: Coexistência com WiFi, status broadcast, fila otimizada
- 🔥 **Firebase**: Estrutura de presença para bot monitorar