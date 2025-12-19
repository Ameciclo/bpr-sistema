# 🗺️ Mapa de Chamadas de Funções - Hub Firmware

Este documento mapeia todas as chamadas de funções no firmware do hub, partindo da função `main()` e traçando a trajetória completa de execução.

## 📋 Estrutura Geral

```
main.cpp
├── setup()
└── loop()
```

---

## 🚀 Função `setup()` - Inicialização

### Fluxo Principal de Inicialização

```
setup() [main.cpp]
├── Serial.begin(115200)
├── LittleFS.begin()
├── SelfCheck::systemCheck() [self_check.cpp]
│   ├── SelfCheck::checkMemory()
│   ├── SelfCheck::checkFileSystem()
│   ├── SelfCheck::checkLED()
│   ├── SelfCheck::checkWiFi()
│   └── SelfCheck::checkBLE()
├── ConfigManager::loadConfig() [config_manager.cpp]
│   ├── LittleFS.exists(CONFIG_FILE)
│   ├── LittleFS.open(CONFIG_FILE, "r")
│   ├── deserializeJson(doc, file)
│   └── ConfigManager::isConfigValid()
├── BufferManager::begin() [buffer_manager.cpp]
│   ├── BufferManager::loadBuffer()
│   ├── BufferManager::createBackup()
│   └── BufferManager::cleanupOldBackups()
├── LEDController::begin() [led_controller.cpp]
│   ├── pinMode(LED_PIN, OUTPUT)
│   └── digitalWrite(LED_PIN, LOW)
├── LEDController::bootPattern()
│   └── LEDController::setPattern(PATTERN_BOOT)
└── changeState(STATE_CONFIG_AP | STATE_CLOUD_SYNC)
    └── [Ver seção Estados abaixo]
```

---

## 🔄 Função `loop()` - Loop Principal

### Fluxo Principal do Loop

```
loop() [main.cpp]
├── LEDController::update() [led_controller.cpp]
│   ├── millis() - patternStartTime
│   ├── switch(currentPattern)
│   │   ├── PATTERN_BOOT → digitalWrite(LED_PIN, HIGH/LOW)
│   │   ├── PATTERN_CONFIG → Fast blink pattern
│   │   ├── PATTERN_BLE_READY → Slow blink pattern
│   │   ├── PATTERN_SYNC → Medium blink pattern
│   │   ├── PATTERN_ERROR → Fast error blink
│   │   ├── PATTERN_BIKE_ARRIVED → updateBlinkPattern(3 blinks)
│   │   ├── PATTERN_BIKE_LEFT → Long blink + return to BLE_READY
│   │   └── PATTERN_COUNT → updateBlinkPattern(N blinks)
│   └── LEDController::updateBlinkPattern()
├── BufferManager::isCriticallyFull() [buffer_manager.cpp]
│   └── (dataCount >= criticalThreshold)
├── checkPeriodicSync()
│   ├── millis() - lastSyncCheck
│   ├── BufferManager::needsSync()
│   ├── BikePairing::isSafeToExit()
│   └── changeState(STATE_CLOUD_SYNC)
├── switch(currentState)
│   ├── STATE_CONFIG_AP → ConfigAP::update()
│   ├── STATE_BIKE_PAIRING → BikePairing::update()
│   └── STATE_CLOUD_SYNC → CloudSync::update()
├── SyncMonitor::shouldFallback()
└── printStatus() [a cada 30s]
```

---

## 🏛️ Máquina de Estados - `changeState()`

### Transições de Estado

```
changeState(newState) [main.cpp]
├── getStateName(currentState)
├── Exit Current State:
│   ├── STATE_CONFIG_AP → ConfigAP::exit()
│   ├── STATE_BIKE_PAIRING → BikePairing::exit()
│   └── STATE_CLOUD_SYNC → CloudSync::exit()
├── currentState = newState
├── stateStartTime = millis()
└── Enter New State:
    ├── STATE_CONFIG_AP → ConfigAP::enter(isInitialMode)
    ├── STATE_BIKE_PAIRING → BikePairing::enter()
    └── STATE_CLOUD_SYNC → CloudSync::enter() → handleSyncResult()
```

---

## 🔧 Estado: CONFIG_AP

### ConfigAP::enter() [config_ap.cpp]

```
ConfigAP::enter(isInitialMode) [config_ap.cpp]
├── WiFi.mode(WIFI_AP)
├── WiFi.softAP(AP_SSID, AP_PASSWORD)
├── WiFi.onEvent() [Callbacks para conexões]
├── ConfigAP::setupWebServer()
│   ├── server.on("/", HTTP_GET) → HTML form handler
│   ├── server.on("/save", HTTP_POST) → Form submission handler
│   │   ├── ConfigManager::getConfig()
│   │   ├── strcpy() [múltiplas para campos]
│   │   ├── ConfigManager::saveConfig()
│   │   ├── ConfigAP::tryUpdateWiFiInFirebase()
│   │   │   ├── WiFi.begin(ssid, password)
│   │   │   ├── HTTPClient::begin(url)
│   │   │   ├── HTTPClient::PUT(jsonString)
│   │   │   └── WiFi.softAP() [volta para AP]
│   │   └── ESP.restart()
│   ├── server.on("/status", HTTP_GET) → Status JSON
│   └── server.on("/save-json", HTTP_POST) → JSON config handler
├── server.begin()
├── apStartTime = millis()
└── LEDController::configPattern()
```

### ConfigAP::update() [config_ap.cpp]

```
ConfigAP::update() [config_ap.cpp]
├── server.handleClient()
├── millis() - apStartTime
├── configManager.getConfig().timeouts.config_ap_min
└── if (timeout) → ESP.restart() | return
```

### ConfigAP::exit() [config_ap.cpp]

```
ConfigAP::exit() [config_ap.cpp]
├── server.stop()
├── WiFi.softAPdisconnect(true)
└── WiFi.removeEvent()
```

---

## 🚲 Estado: BIKE_PAIRING

### BikePairing::enter() [bike_pairing.cpp]

```
BikePairing::enter() [bike_pairing.cpp]
├── BikeManager::init() [bike_manager.cpp]
├── currentStatus = PAIRING_IDLE
├── lastActivity = millis()
├── BLEServer::start() [ble_server.cpp]
│   ├── NimBLEDevice::init(BLE_DEVICE_NAME)
│   ├── NimBLEDevice::setPower(ESP_PWR_LVL_P3)
│   ├── NimBLEDevice::createServer()
│   ├── pServer->setCallbacks(new ServerCallbacks())
│   ├── pService->createService(BLE_SERVICE_UUID)
│   ├── pDataChar->createCharacteristic(BLE_CHAR_DATA_UUID)
│   ├── pConfigChar->createCharacteristic(BLE_CHAR_CONFIG_UUID)
│   ├── pDataChar->setCallbacks(new DataCallbacks())
│   ├── pConfigChar->setCallbacks(new ConfigCallbacks())
│   ├── pService->start()
│   └── NimBLEDevice::startAdvertising()
└── LEDController::bikePairingPattern()
```

### BikePairing::update() [bike_pairing.cpp]

```
BikePairing::update() [bike_pairing.cpp]
├── BikePairing::processDataQueue()
│   ├── millis() - requestTimeout > BIKE_TIMEOUT_MS
│   ├── BikePairing::finishCurrentBike()
│   └── BikePairing::requestDataFromBike()
├── millis() - lastHeartbeat > HEARTBEAT_INTERVAL
├── BikePairing::sendHeartbeat()
│   ├── DynamicJsonDocument heartbeat(1024)
│   ├── BikeManager::populateHeartbeatData(bikes)
│   ├── BLEServer::getConnectedBikes()
│   ├── BikeManager::getAllowedCount()
│   ├── BikeManager::getPendingCount()
│   └── BufferManager::addHeartbeat()
└── LEDController::countPattern(connectedBikes) [a cada 30s]
```

### BikePairing::exit() [bike_pairing.cpp]

```
BikePairing::exit() [bike_pairing.cpp]
├── while (!dataQueue.empty()) → dataQueue.pop()
├── currentBike = ""
├── requestTimeout = 0
├── BLEServer::stop()
└── currentStatus = PAIRING_IDLE
```

### Callbacks BLE [bike_pairing.cpp]

```
BLEServer::onBikeConnected(bikeId) [bike_pairing.cpp]
├── LEDController::bikeArrivedPattern()
├── BikeManager::canConnect(bikeId)
├── BLEServer::forceDisconnectBike(bikeId) [se blocked]
├── BikeManager::hasConfigUpdate(bikeId)
├── BikeManager::getConfigForBike(bikeId)
├── BLEServer::pushConfigToBike(bikeId, config)
└── BikeManager::markConfigSent(bikeId)

BLEServer::onBikeDisconnected(bikeId) [bike_pairing.cpp]
└── LEDController::bikeLeftPattern()

BLEServer::onBikeDataReceived(bikeId, jsonData) [bike_pairing.cpp]
├── BikeManager::canConnect(bikeId)
├── BikeManager::isAllowed(bikeId)
├── BikeManager::recordPendingVisit(bikeId)
├── BikePairing::processDataFromBike(bikeId, jsonData)
│   ├── deserializeJson(doc, jsonData)
│   ├── BikeManager::updateHeartbeat(bikeId, battery, heap)
│   ├── BufferManager::addBikeData(bikeId, jsonData)
│   ├── BikeManager::hasConfigUpdate(bikeId)
│   ├── BLEServer::pushConfigToBike(bikeId, config)
│   └── BikePairing::finishCurrentBike()
└── BikePairing::enqueueBike(bikeId, jsonData)

BLEServer::onConfigRequest(bikeId, request) [bike_pairing.cpp]
├── deserializeJson(doc, request)
├── type == "config_request"
│   ├── BikeManager::hasConfigUpdate(bikeId)
│   ├── BikeManager::getConfigForBike(bikeId)
│   ├── BLEServer::pushConfigToBike(bikeId, config)
│   └── BikeManager::markConfigSent(bikeId)
└── type == "config_received"
    └── currentStatus = PAIRING_IDLE
```

---

## ☁️ Estado: CLOUD_SYNC

### CloudSync::enter() [cloud_sync.cpp]

```
CloudSync::enter() [cloud_sync.cpp]
├── LEDController::syncPattern()
├── CloudSync::connectWiFi()
│   ├── WiFi.mode(WIFI_STA)
│   ├── WiFi.begin(ssid, password)
│   └── while (WiFi.status() != WL_CONNECTED)
├── CloudSync::syncTime()
│   ├── configTime(timezone, 0, ntpServer)
│   └── while (!time(nullptr))
├── CloudSync::downloadCentralConfig()
│   ├── HTTPClient::begin(configUrl)
│   ├── HTTPClient::GET()
│   ├── ConfigManager::updateFromJson(payload)
│   └── ConfigManager::isValidFirebaseConfig()
├── CloudSync::downloadBikeData()
├── CloudSync::uploadBufferData()
│   ├── BufferManager::getDataForUpload(doc)
│   ├── HTTPClient::begin(dataUrl)
│   ├── HTTPClient::POST(jsonString)
│   ├── BufferManager::markAsConfirmed() [se sucesso]
│   └── BufferManager::rollbackUpload() [se falha]
├── CloudSync::uploadHeartbeat()
│   ├── DynamicJsonDocument heartbeat
│   ├── HTTPClient::begin(heartbeatUrl)
│   └── HTTPClient::PUT(jsonString)
├── CloudSync::uploadBikeData()
├── CloudSync::uploadWiFiConfig() [se firstSync]
└── return SyncResult::SUCCESS | SyncResult::FAILURE
```

### CloudSync::update() [cloud_sync.cpp]

```
CloudSync::update() [cloud_sync.cpp]
├── millis() - stateStartTime > timeout
└── handleSyncResult(SyncResult::FAILURE)
```

### CloudSync::exit() [cloud_sync.cpp]

```
CloudSync::exit() [cloud_sync.cpp]
├── WiFi.disconnect(true)
└── WiFi.mode(WIFI_OFF)
```

---

## 📊 Módulos de Suporte

### BikeManager [bike_manager.cpp]

```
BikeManager::init()
└── BikeManager::loadData()
    ├── LittleFS.exists(BIKE_DATA_FILE)
    ├── LittleFS.open(BIKE_DATA_FILE, "r")
    ├── deserializeJson(bikes, file)
    └── dataLoaded = true

BikeManager::canConnect(bikeId)
├── bikeId.startsWith("bpr-") && bikeId.length() == 10
├── bikes.containsKey(bikeId)
├── BikeManager::addPendingBike(bikeId) [se nova]
└── status != "blocked"

BikeManager::isAllowed(bikeId)
├── bikeId.startsWith("bpr-") && bikeId.length() == 10
├── bikes.containsKey(bikeId)
└── status == "allowed"

BikeManager::updateHeartbeat(bikeId, battery, heap)
├── time(nullptr)
├── getLocalTime(&timeinfo)
├── strftime(dateStr, ...)
├── bikes[bikeId]["last_heartbeat"]["timestamp"] = now
├── bikes[bikeId]["last_heartbeat"]["battery"] = battery
└── bikes[bikeId]["last_heartbeat"]["heap"] = heap

BikeManager::downloadFromFirebase()
├── HTTPClient::begin(bike_configs_url)
├── HTTPClient::GET()
├── deserializeJson(newConfigs, payload)
├── bikes[bikeId]["config"] = bike.value()
├── configChanged[bikeId] = true [se version mudou]
└── BikeManager::saveData()

BikeManager::hasConfigUpdate(bikeId)
└── configChanged[bikeId] == true

BikeManager::getConfigForBike(bikeId)
├── bikes[bikeId]["config"].isNull() → generateDefaultConfig()
├── response["type"] = "config_push"
├── response["bike_id"] = bikeId
├── response["config"] = bikes[bikeId]["config"]
└── serializeJson(response, result)
```

### ConfigManager [config_manager.cpp]

```
ConfigManager::loadConfig()
├── LittleFS.exists(CONFIG_FILE)
├── LittleFS.open(CONFIG_FILE, "r")
├── deserializeJson(doc, file)
├── [Múltiplas atribuições de campos]
└── ConfigManager::isConfigValid()

ConfigManager::saveConfig()
├── DynamicJsonDocument doc(2048)
├── [Múltiplas atribuições para doc]
├── LittleFS.open(CONFIG_FILE, "w")
└── serializeJson(doc, file)

ConfigManager::updateFromFirebase(firebaseConfig)
├── [Múltiplas atribuições de campos do Firebase]
├── ConfigManager::saveConfig()
└── Serial.printf() [logs de atualização]

ConfigManager::isValidFirebaseConfig(doc)
├── doc["intervals"]["sync_sec"]
├── doc["timeouts"]["wifi_sec"]
├── doc["led"]["ble_ready_ms"]
├── doc["limits"]["max_bikes"]
└── doc["fallback"]["max_failures"]
```

### BufferManager [buffer_manager.cpp]

```
BufferManager::begin()
├── BufferManager::loadBuffer()
├── BufferManager::createBackup()
└── BufferManager::cleanupOldBackups()

BufferManager::addBikeData(bikeId, jsonData)
├── deserializeJson(doc, jsonData)
├── time(nullptr)
├── getLocalTime(&timeinfo)
├── strftime(dateStr, ...)
├── doc["central_receive_timestamp"] = now
├── doc["central_receive_timestamp_human"] = dateStr
├── serializeJson(doc, modifiedJson)
└── BufferManager::addData(bikeId, modifiedJson.c_str(), length)
    ├── CRC32::update(finalData, finalSize)
    ├── buffer[dataCount].bikeId = bikeId
    ├── buffer[dataCount].timestamp = time(nullptr)
    ├── buffer[dataCount].crc32 = checksum
    ├── memcpy(buffer[dataCount].data, finalData, finalSize)
    ├── dataCount++
    └── BufferManager::saveBuffer() [a cada 5 itens]

BufferManager::needsSync()
├── dataCount > 0
├── (dataCount * 100 / maxSize) >= syncThreshold
└── millis() - lastSync > autoSaveInterval

BufferManager::getDataForUpload(doc)
├── JsonArray items = doc.createNestedArray("items")
├── for (int i = 0; i < dataCount; i++)
├── JsonObject item = items.createNestedObject()
└── [Serialização de todos os itens]

BufferManager::markAsConfirmed()
├── BufferManager::createBackup()
├── dataCount = 0
├── lastSync = millis()
└── BufferManager::saveBuffer()
```

### LEDController [led_controller.cpp]

```
LEDController::update()
├── millis() - patternStartTime
├── switch(currentPattern)
│   ├── PATTERN_BOOT → digitalWrite(LED_PIN, HIGH/LOW)
│   ├── PATTERN_CONFIG → Fast blink (200ms cycle)
│   ├── PATTERN_BLE_READY → Slow blink (configurable)
│   ├── PATTERN_SYNC → Medium blink (configurable)
│   ├── PATTERN_ERROR → Fast error blink (configurable)
│   ├── PATTERN_BIKE_ARRIVED → updateBlinkPattern(3, 150, 100)
│   ├── PATTERN_BIKE_LEFT → 1000ms HIGH + return to BLE_READY
│   └── PATTERN_COUNT → updateBlinkPattern(targetBlinks, ...)
└── LEDController::updateBlinkPattern()

LEDController::updateBlinkPattern(elapsed, maxBlinks, onTime, offTime)
├── cycleTime = onTime + offTime
├── currentCycle = elapsed / cycleTime
├── cyclePosition = elapsed % cycleTime
├── if (currentCycle < maxBlinks)
│   ├── if (cyclePosition < onTime) → digitalWrite(LED_PIN, HIGH)
│   └── else → digitalWrite(LED_PIN, LOW)
└── if (pattern finished) → setPattern(PATTERN_BLE_READY)
```

---

## 🔍 Funções Auxiliares

### handleSyncResult() [main.cpp]

```
handleSyncResult(result) [main.cpp]
├── switch(result)
├── SyncResult::SUCCESS
│   ├── firstSync = false
│   └── changeState(STATE_BIKE_PAIRING)
└── SyncResult::FAILURE
    ├── if (firstSync) → changeState(STATE_CONFIG_AP)
    └── else → changeState(STATE_BIKE_PAIRING)
```

### printStatus() [main.cpp]

```
printStatus() [main.cpp]
├── configManager.getConfig().base_id
├── getStateName(currentState)
├── millis() / 1000 [uptime]
├── if (STATE_CONFIG_AP)
│   └── Serial.println() [AP info]
├── else
│   ├── BikePairing::getConnectedBikes()
│   ├── ESP.getFreeHeap()
│   ├── configManager.getConfig().sync_interval_ms()
│   └── (millis() - stateStartTime) / 1000
└── Serial.printf() [status completo]
```

### checkPeriodicSync() [main.cpp]

```
checkPeriodicSync() [main.cpp]
├── if (currentState != STATE_BIKE_PAIRING) → return
├── millis() - lastSyncCheck <= sync_interval → return
├── lastSyncCheck = millis()
├── BufferManager::needsSync() → return if false
├── BikePairing::isSafeToExit()
│   ├── BikePairing::getStatus() == PAIRING_IDLE
│   └── (millis() - lastActivity) > busyTimeout
└── changeState(STATE_CLOUD_SYNC)
```

---

## 📈 Fluxo de Dados Completo

### 1. Inicialização
```
setup() → SelfCheck → ConfigManager::loadConfig() → BufferManager::begin() → LEDController::begin() → changeState()
```

### 2. Operação Normal (BIKE_PAIRING)
```
loop() → BikePairing::update() → BLE callbacks → BufferManager::addBikeData() → checkPeriodicSync() → changeState(CLOUD_SYNC)
```

### 3. Sincronização (CLOUD_SYNC)
```
CloudSync::enter() → connectWiFi() → downloadConfigs() → uploadData() → uploadHeartbeat() → handleSyncResult() → changeState(BIKE_PAIRING)
```

### 4. Configuração (CONFIG_AP)
```
ConfigAP::enter() → setupWebServer() → server.handleClient() → saveConfig() → ESP.restart()
```

---

## 🎯 Pontos de Entrada Principais

1. **setup()** - Inicialização única do sistema
2. **loop()** - Execução contínua da máquina de estados
3. **BLE Callbacks** - Eventos assíncronos de bikes
4. **Web Server Handlers** - Configuração via HTTP
5. **Timer Callbacks** - Sync periódico e heartbeat

---

## 📝 Observações

- **Estado Assíncrono**: BLE callbacks podem interromper o fluxo normal
- **Timeouts**: Cada estado tem timeouts para evitar travamentos
- **Fallbacks**: Sistema volta para CONFIG_AP em caso de falhas críticas
- **Persistência**: Dados são salvos em LittleFS para sobreviver a reinicializações
- **Configuração Dinâmica**: Parâmetros podem ser atualizados via Firebase
- **LED Feedback**: Padrões visuais indicam o estado atual do sistema

Este mapa fornece uma visão completa de como as funções se relacionam e são chamadas no firmware do hub, facilitando debugging e desenvolvimento de novas funcionalidades.