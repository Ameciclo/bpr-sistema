# 📊 Diagrama de Blocos de Chamadas de Funções - Hub Firmware

## 🔄 Fluxo Principal

### 🚀 Setup Phase
```mermaid
flowchart TD
    A[main] --> B[setup]
    B --> D[Serial.begin]
    D --> E[LittleFS.begin]
    E --> F[SelfCheck::systemCheck]
    F --> G[ConfigManager::loadConfig]
    G --> H[BufferManager::begin]
    H --> I[LEDController::begin]
    I --> J[LEDController::bootPattern]
    J --> K[changeState]
    
    F --> F1[checkMemory]
    F1 --> F2[checkFileSystem]
    F2 --> F3[checkLED]
    F3 --> F4[checkWiFi]
    F4 --> F5[checkBLE]
    
    G --> G1[LittleFS.exists]
    G1 --> G2[LittleFS.open]
    G2 --> G3[deserializeJson]
    G3 --> G4[isConfigValid]
    
    H --> H1[loadBuffer]
    H1 --> H2[createBackup]
    H2 --> H3[cleanupOldBackups]
    
    I --> I1[pinMode]
    I1 --> I2[digitalWrite]
    
    J --> J1[setPattern]
    
    style A fill:#e1f5fe
    style B fill:#f3e5f5
    style F fill:#fff3e0
    style G fill:#fff3e0
    style H fill:#fff3e0
    style I fill:#fff3e0
```

### 🔄 Loop Phase
```mermaid
flowchart TD
    A[main] --> C[loop]
    C --> L[LEDController::update]
    L --> M[BufferManager::isCriticallyFull]
    M --> N[checkPeriodicSync]
    N --> O[switch currentState]
    O --> P[SyncMonitor::shouldFallback]
    P --> Q[printStatus]
    
    L --> L1[millis check]
    L1 --> L2[switch patterns]
    L2 --> L3[updateBlinkPattern]
    
    N --> N1[millis check]
    N1 --> N2[BufferManager::needsSync]
    N2 --> N3[BikePairing::isSafeToExit]
    N3 --> N4[changeState CLOUD_SYNC]
    
    O --> O1[STATE_CONFIG_AP]
    O --> O2[STATE_BIKE_PAIRING]
    O --> O3[STATE_CLOUD_SYNC]
    
    O1 --> ConfigAP_update[ConfigAP::update]
    O2 --> BikePairing_update[BikePairing::update]
    O3 --> CloudSync_update[CloudSync::update]
    
    style C fill:#e8f5e8
    style L fill:#fff3e0
    style N fill:#f3e5f5
    style O fill:#ffebee
```

### 💡 LED Patterns
```mermaid
flowchart TD
    A[switch patterns] --> B[PATTERN_BOOT]
    A --> C[PATTERN_CONFIG]
    A --> D[PATTERN_BLE_READY]
    A --> E[PATTERN_SYNC]
    A --> F[PATTERN_ERROR]
    A --> G[PATTERN_BIKE_ARRIVED]
    A --> H[PATTERN_BIKE_LEFT]
    A --> I[PATTERN_COUNT]
    
    B --> B1[digitalWrite HIGH/LOW]
    C --> C1[Fast blink]
    D --> D1[Slow blink]
    E --> E1[Medium blink]
    F --> F1[Fast error blink]
    G --> G1[3 blinks]
    H --> H1[Long blink]
    I --> I1[N blinks]
    
    style A fill:#fff3e0
```

## 🏛️ Estados - changeState()

```mermaid
flowchart TD
    A[changeState newState] --> B[getStateName currentState]
    A --> C[Exit Current State]
    A --> D[currentState = newState]
    A --> E[stateStartTime = millis]
    A --> F[Enter New State]
    
    C --> C1[STATE_CONFIG_AP]
    C --> C2[STATE_BIKE_PAIRING]
    C --> C3[STATE_CLOUD_SYNC]
    
    C1 --> C1A[ConfigAP::exit]
    C2 --> C2A[BikePairing::exit]
    C3 --> C3A[CloudSync::exit]
    
    F --> F1[STATE_CONFIG_AP]
    F --> F2[STATE_BIKE_PAIRING]
    F --> F3[STATE_CLOUD_SYNC]
    
    F1 --> F1A[ConfigAP::enter isInitialMode]
    F2 --> F2A[BikePairing::enter]
    F3 --> F3A[CloudSync::enter]
    
    F3A --> F3B[handleSyncResult]
    
    style A fill:#e1f5fe
    style C fill:#ffebee
    style F fill:#e8f5e8
```

## 🔧 Estado CONFIG_AP

### 🚀 ConfigAP::enter
```mermaid
flowchart TD
    A[ConfigAP::enter isInitialMode] --> B[WiFi.mode WIFI_AP]
    B --> C[WiFi.softAP]
    C --> D[WiFi.onEvent Callbacks]
    D --> E[setupWebServer]
    E --> F[server.begin]
    F --> G[apStartTime = millis]
    G --> H[LEDController::configPattern]
    
    E --> E1[server.on / HTTP_GET]
    E1 --> E2[server.on /save HTTP_POST]
    E2 --> E3[server.on /status HTTP_GET]
    E3 --> E4[server.on /save-json HTTP_POST]
    
    style A fill:#e1f5fe
    style E fill:#fff3e0
```

### 💾 Save Handler
```mermaid
flowchart TD
    A[/save HTTP_POST] --> B[ConfigManager::getConfig]
    B --> C[strcpy múltiplas]
    C --> D[ConfigManager::saveConfig]
    D --> E[tryUpdateWiFiInFirebase]
    E --> F[ESP.restart]
    
    E --> E1[WiFi.begin]
    E1 --> E2[HTTPClient::begin]
    E2 --> E3[HTTPClient::PUT]
    E3 --> E4[WiFi.softAP volta AP]
    
    style A fill:#f3e5f5
    style E fill:#fff3e0
```

### 🔄 ConfigAP::update & exit
```mermaid
flowchart TD
    A[ConfigAP::update] --> B[server.handleClient]
    B --> C[millis - apStartTime]
    C --> D[check timeout]
    D --> E{timeout?}
    E -->|Yes| F[ESP.restart]
    E -->|No| G[return]
    
    H[ConfigAP::exit] --> I[server.stop]
    I --> J[WiFi.softAPdisconnect]
    J --> K[WiFi.removeEvent]
    
    style A fill:#fff3e0
    style H fill:#ffebee
```

## 🚲 Estado BIKE_PAIRING

### 🚀 BikePairing::enter
```mermaid
flowchart TD
    A[BikePairing::enter] --> B[BikeManager::init]
    B --> C[currentStatus = PAIRING_IDLE]
    C --> D[lastActivity = millis]
    D --> E[BLEServer::start]
    E --> F[LEDController::bikePairingPattern]
    
    B --> B1[BikeManager::loadData]
    B1 --> B1A[LittleFS.exists BIKE_DATA_FILE]
    B1A --> B1B[LittleFS.open r]
    B1B --> B1C[deserializeJson bikes]
    B1C --> B1D[dataLoaded = true]
    
    style A fill:#e1f5fe
    style B fill:#fff3e0
```

### 📡 BLE Server Setup
```mermaid
flowchart TD
    A[BLEServer::start] --> B[NimBLEDevice::init]
    B --> C[NimBLEDevice::setPower]
    C --> D[NimBLEDevice::createServer]
    D --> E[setCallbacks ServerCallbacks]
    E --> F[createService BLE_SERVICE_UUID]
    F --> G[createCharacteristic DATA_UUID]
    G --> H[createCharacteristic CONFIG_UUID]
    H --> I[setCallbacks DataCallbacks]
    I --> J[setCallbacks ConfigCallbacks]
    J --> K[pService->start]
    K --> L[startAdvertising]
    
    style A fill:#e1f5fe
```

### 🔄 BikePairing::update
```mermaid
flowchart TD
    A[BikePairing::update] --> B[processDataQueue]
    B --> C[check heartbeat interval]
    C --> D[sendHeartbeat]
    D --> E[LEDController::countPattern]
    
    B --> B1[check timeout]
    B1 --> B2[finishCurrentBike]
    B2 --> B3[requestDataFromBike]
    
    D --> D1[DynamicJsonDocument heartbeat]
    D1 --> D2[populateHeartbeatData]
    D2 --> D3[getConnectedBikes]
    D3 --> D4[getAllowedCount]
    D4 --> D5[getPendingCount]
    D5 --> D6[BufferManager::addHeartbeat]
    
    style A fill:#fff3e0
```

### 🚪 BikePairing::exit
```mermaid
flowchart TD
    A[BikePairing::exit] --> B[clear dataQueue]
    B --> C[currentBike = empty]
    C --> D[requestTimeout = 0]
    D --> E[BLEServer::stop]
    E --> F[currentStatus = PAIRING_IDLE]
    
    style A fill:#ffebee
```

## 📡 Callbacks BLE

### 🔌 onBikeConnected
```mermaid
flowchart TD
    A[onBikeConnected bikeId] --> B[LEDController::bikeArrivedPattern]
    B --> C[BikeManager::canConnect]
    C --> D{blocked?}
    D -->|Yes| D1[forceDisconnectBike]
    D -->|No| E[BikeManager::hasConfigUpdate]
    E --> F[BikeManager::getConfigForBike]
    F --> G[BLEServer::pushConfigToBike]
    G --> H[BikeManager::markConfigSent]
    
    C --> C1[check bpr- prefix]
    C1 --> C2[check length == 10]
    C2 --> C3[bikes.containsKey]
    C3 --> C4[addPendingBike if new]
    C4 --> C5[status != blocked]
    
    style A fill:#e1f5fe
```

### 📤 Config Generation
```mermaid
flowchart TD
    A[getConfigForBike] --> B{config.isNull?}
    B -->|Yes| C[generateDefaultConfig]
    B -->|No| D[use existing config]
    C --> E[response type = config_push]
    D --> E
    E --> F[response bike_id = bikeId]
    F --> G[response config = bikes config]
    G --> H[serializeJson response]
    
    style A fill:#fff3e0
```

### 🔌 onBikeDisconnected
```mermaid
flowchart TD
    A[onBikeDisconnected bikeId] --> B[LEDController::bikeLeftPattern]
    
    style A fill:#fff3e0
```

### 📥 onBikeDataReceived - Validation
```mermaid
flowchart TD
    A[onBikeDataReceived bikeId jsonData] --> B[BikeManager::canConnect]
    B --> C[BikeManager::isAllowed]
    C --> D[BikeManager::recordPendingVisit]
    D --> E[processDataFromBike]
    E --> F[enqueueBike]
    
    C --> C1[check bpr- prefix]
    C1 --> C2[check length == 10]
    C2 --> C3[bikes.containsKey]
    C3 --> C4[status == allowed]
    
    style A fill:#e8f5e8
```

### 💾 Data Processing
```mermaid
flowchart TD
    A[processDataFromBike] --> B[deserializeJson]
    B --> C[updateHeartbeat]
    C --> D[addBikeData]
    D --> E[hasConfigUpdate]
    E --> F[pushConfigToBike]
    F --> G[finishCurrentBike]
    
    C --> C1[time nullptr]
    C1 --> C2[getLocalTime]
    C2 --> C3[strftime]
    C3 --> C4[set timestamp]
    C4 --> C5[set battery]
    C5 --> C6[set heap]
    
    style A fill:#fff3e0
```

### 🗄️ Buffer Management
```mermaid
flowchart TD
    A[addBikeData] --> B[deserializeJson]
    B --> C[time nullptr]
    C --> D[getLocalTime]
    D --> E[strftime]
    E --> F[set central_receive_timestamp]
    F --> G[set central_receive_timestamp_human]
    G --> H[serializeJson]
    H --> I[addData]
    
    I --> I1[CRC32::update]
    I1 --> I2[set bikeId]
    I2 --> I3[set timestamp]
    I3 --> I4[set crc32]
    I4 --> I5[memcpy data]
    I5 --> I6[dataCount++]
    I6 --> I7[saveBuffer every 5]
    
    style A fill:#e8f5e8
```

### ⚙️ onConfigRequest
```mermaid
flowchart TD
    A[onConfigRequest bikeId request] --> B[deserializeJson]
    B --> C{type?}
    
    C -->|config_request| D[hasConfigUpdate]
    D --> E[getConfigForBike]
    E --> F[pushConfigToBike]
    F --> G[markConfigSent]
    
    C -->|config_received| H[currentStatus = PAIRING_IDLE]
    
    style A fill:#f3e5f5
```

## ☁️ Estado CLOUD_SYNC

### 🚀 CloudSync::enter - Initialization
```mermaid
flowchart TD
    A[CloudSync::enter] --> B[LEDController::syncPattern]
    B --> C[connectWiFi]
    C --> D[syncTime]
    D --> E[downloadCentralConfig]
    E --> F[downloadBikeData]
    F --> G[uploadBufferData]
    G --> H[uploadHeartbeat]
    H --> I[uploadBikeData]
    I --> J[uploadWiFiConfig if firstSync]
    J --> K[return SyncResult]
    
    style A fill:#e1f5fe
```

### 📶 WiFi Connection
```mermaid
flowchart TD
    A[connectWiFi] --> B[WiFi.mode WIFI_STA]
    B --> C[WiFi.begin ssid password]
    C --> D[while not connected]
    
    E[syncTime] --> F[configTime timezone ntpServer]
    F --> G[while not time nullptr]
    
    style A fill:#fff3e0
    style E fill:#fff3e0
```

### ⬇️ Download Config
```mermaid
flowchart TD
    A[downloadCentralConfig] --> B[HTTPClient::begin configUrl]
    B --> C[HTTPClient::GET]
    C --> D[updateFromJson payload]
    D --> E[isValidFirebaseConfig]
    
    D --> D1[múltiplas atribuições Firebase]
    D1 --> D2[saveConfig]
    D2 --> D3[Serial.printf logs]
    
    D2 --> D2A[DynamicJsonDocument doc]
    D2A --> D2B[múltiplas atribuições doc]
    D2B --> D2C[LittleFS.open w]
    D2C --> D2D[serializeJson doc file]
    
    style A fill:#e8f5e8
```

### 🚲 Download Bike Data
```mermaid
flowchart TD
    A[downloadBikeData] --> B[downloadFromFirebase]
    B --> C[HTTPClient::begin bike_configs_url]
    C --> D[HTTPClient::GET]
    D --> E[deserializeJson newConfigs]
    E --> F[bikes bikeId config = bike.value]
    F --> G[configChanged bikeId = true if version changed]
    G --> H[saveData]
    
    style A fill:#e8f5e8
```

### ⬆️ Upload Buffer Data
```mermaid
flowchart TD
    A[uploadBufferData] --> B[getDataForUpload doc]
    B --> C[HTTPClient::begin dataUrl]
    C --> D[HTTPClient::POST jsonString]
    D --> E{success?}
    
    B --> B1[JsonArray items = createNestedArray]
    B1 --> B2[for i = 0 to dataCount]
    B2 --> B3[JsonObject item = createNestedObject]
    B3 --> B4[serialização todos itens]
    
    E -->|Yes| F[markAsConfirmed]
    E -->|No| G[rollbackUpload]
    
    F --> F1[createBackup]
    F1 --> F2[dataCount = 0]
    F2 --> F3[lastSync = millis]
    F3 --> F4[saveBuffer]
    
    style A fill:#f3e5f5
```

### 💓 Upload Heartbeat
```mermaid
flowchart TD
    A[uploadHeartbeat] --> B[DynamicJsonDocument heartbeat]
    B --> C[HTTPClient::begin heartbeatUrl]
    C --> D[HTTPClient::PUT jsonString]
    
    style A fill:#f3e5f5
```

### 🔄 CloudSync::update & exit
```mermaid
flowchart TD
    A[CloudSync::update] --> B[check timeout]
    B --> C{timeout?}
    C -->|Yes| D[handleSyncResult FAILURE]
    
    E[CloudSync::exit] --> F[WiFi.disconnect true]
    F --> G[WiFi.mode WIFI_OFF]
    
    style A fill:#fff3e0
    style E fill:#ffebee
```

## 🔍 Funções Auxiliares

```mermaid
flowchart TD
    subgraph "handleSyncResult"
        A[handleSyncResult result] --> B{switch result}
        
        B -->|SUCCESS| C[firstSync = false]
        B -->|SUCCESS| D[changeState BIKE_PAIRING]
        
        B -->|FAILURE| E{firstSync?}
        E -->|Yes| F[changeState CONFIG_AP]
        E -->|No| G[changeState BIKE_PAIRING]
    end
    
    subgraph "printStatus"
        H[printStatus] --> I[get base_id]
        H --> J[getStateName currentState]
        H --> K[millis / 1000 uptime]
        H --> L{STATE_CONFIG_AP?}
        
        L -->|Yes| M[Serial.println AP info]
        L -->|No| N[getConnectedBikes]
        L -->|No| O[ESP.getFreeHeap]
        L -->|No| P[get sync_interval_ms]
        L -->|No| Q[millis - stateStartTime / 1000]
        L -->|No| R[Serial.printf status completo]
    end
    
    subgraph "checkPeriodicSync"
        S[checkPeriodicSync] --> T{currentState == BIKE_PAIRING?}
        T -->|No| U[return]
        
        T -->|Yes| V{millis - lastSyncCheck <= sync_interval?}
        V -->|Yes| W[return]
        
        V -->|No| X[lastSyncCheck = millis]
        X --> Y[needsSync]
        X --> Z[isSafeToExit]
        X --> AA[changeState CLOUD_SYNC]
        
        Y --> Y1[dataCount > 0]
        Y --> Y2[dataCount * 100 / maxSize >= syncThreshold]
        Y --> Y3[millis - lastSync > autoSaveInterval]
        
        Z --> Z1[getStatus == PAIRING_IDLE]
        Z --> Z2[millis - lastActivity > busyTimeout]
    end
    
    style A fill:#e1f5fe
    style H fill:#fff3e0
    style S fill:#e8f5e8
```

## 📊 Resumo de Conexões por Arquivo

```mermaid
flowchart TD
    subgraph "Core"
        MAIN[main.cpp]
    end
    
    subgraph "System Modules"
        SC[SelfCheck]
        CM[ConfigManager]
        BM[BufferManager]
        LED[LEDController]
    end
    
    subgraph "State Handlers"
        CAP[ConfigAP]
        BP[BikePairing]
        CS[CloudSync]
    end
    
    subgraph "Managers"
        BIKE[BikeManager]
        BLE[BLEServer]
    end
    
    subgraph "External APIs"
        FS[LittleFS]
        JSON[ArduinoJson]
        WIFI[WiFi]
        HTTP[HTTPClient]
        CRC[CRC32]
        TIME[time/millis]
        GPIO[digitalWrite/pinMode]
    end
    
    MAIN --> SC
    MAIN --> CM
    MAIN --> BM
    MAIN --> LED
    MAIN --> CAP
    MAIN --> BP
    MAIN --> CS
    
    CM --> FS
    CM --> JSON
    
    BM --> FS
    BM --> JSON
    BM --> CRC
    
    BP --> BIKE
    BP --> BLE
    BP --> BM
    BP --> LED
    
    CS --> WIFI
    CS --> HTTP
    CS --> CM
    CS --> BIKE
    CS --> BM
    CS --> LED
    
    LED --> GPIO
    LED --> TIME
    
    BIKE --> FS
    BIKE --> JSON
    BIKE --> HTTP
    BIKE --> TIME
    
    CAP --> WIFI
    CAP --> HTTP
    CAP --> CM
    CAP --> LED
    
    style MAIN fill:#e1f5fe
    style SC fill:#fff3e0
    style CM fill:#fff3e0
    style BM fill:#fff3e0
    style LED fill:#fff3e0
    style CAP fill:#f3e5f5
    style BP fill:#f3e5f5
    style CS fill:#f3e5f5
    style BIKE fill:#e8f5e8
    style BLE fill:#e8f5e8
```

### 📋 Detalhamento das Conexões

**main.cpp** → Orquestrador principal:
- SelfCheck (systemCheck)
- ConfigManager (loadConfig)
- BufferManager (begin, isCriticallyFull, needsSync)
- LEDController (begin, bootPattern, update)
- ConfigAP (enter, update, exit)
- BikePairing (enter, update, exit, isSafeToExit)
- CloudSync (enter, update, exit)

**config_manager.cpp** → Gerenciamento de configurações:
- LittleFS (exists, open)
- ArduinoJson (deserializeJson, serializeJson)

**buffer_manager.cpp** → Cache local de dados:
- LittleFS (exists, open)
- ArduinoJson (deserializeJson, serializeJson)
- CRC32 (update)

**bike_pairing.cpp** → Comunicação BLE:
- BikeManager (init, canConnect, isAllowed, updateHeartbeat, hasConfigUpdate, getConfigForBike)
- BLEServer (start, stop, getConnectedBikes, pushConfigToBike)
- BufferManager (addBikeData, addHeartbeat)
- LEDController (bikePairingPattern, bikeArrivedPattern, bikeLeftPattern, countPattern)

**cloud_sync.cpp** → Sincronização com Firebase:
- WiFi (mode, begin, status, disconnect)
- HTTPClient (begin, GET, POST, PUT)
- ConfigManager (updateFromJson, isValidFirebaseConfig)
- BikeManager (downloadFromFirebase)
- BufferManager (getDataForUpload, markAsConfirmed, rollbackUpload)
- LEDController (syncPattern)

**led_controller.cpp** → Controle visual:
- digitalWrite, pinMode
- millis()

**bike_manager.cpp** → Gerenciamento de bicicletas:
- LittleFS (exists, open)
- ArduinoJson (deserializeJson, serializeJson)
- HTTPClient (begin, GET)
- time(), getLocalTime()

Este diagrama de blocos mostra todas as conexões entre arquivos e funções, facilitando a visualização do fluxo completo de execução do firmware.