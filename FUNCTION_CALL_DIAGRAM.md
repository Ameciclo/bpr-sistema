# 📊 Diagrama Linear de Chamadas de Funções - Hub Firmware

## 🔄 Fluxo Principal Linear

```
main() 
  ├─ setup()
  │   ├─ Serial.begin(115200)
  │   ├─ LittleFS.begin()
  │   ├─ SelfCheck::systemCheck() ──┐
  │   │                              ├─ SelfCheck::checkMemory()
  │   │                              ├─ SelfCheck::checkFileSystem()
  │   │                              ├─ SelfCheck::checkLED()
  │   │                              ├─ SelfCheck::checkWiFi()
  │   │                              └─ SelfCheck::checkBLE()
  │   ├─ ConfigManager::loadConfig() ──┐
  │   │                                 ├─ LittleFS.exists(CONFIG_FILE)
  │   │                                 ├─ LittleFS.open(CONFIG_FILE, "r")
  │   │                                 ├─ deserializeJson(doc, file)
  │   │                                 └─ ConfigManager::isConfigValid()
  │   ├─ BufferManager::begin() ──┐
  │   │                           ├─ BufferManager::loadBuffer()
  │   │                           ├─ BufferManager::createBackup()
  │   │                           └─ BufferManager::cleanupOldBackups()
  │   ├─ LEDController::begin() ──┐
  │   │                           ├─ pinMode(LED_PIN, OUTPUT)
  │   │                           └─ digitalWrite(LED_PIN, LOW)
  │   ├─ LEDController::bootPattern()
  │   │   └─ LEDController::setPattern(PATTERN_BOOT)
  │   └─ changeState(STATE_CONFIG_AP | STATE_CLOUD_SYNC) ──┐
  │                                                        └─ [Ver Estados]
  └─ loop()
      ├─ LEDController::update() ──┐
      │                            ├─ millis() - patternStartTime
      │                            ├─ switch(currentPattern) ──┐
      │                            │                           ├─ PATTERN_BOOT → digitalWrite(LED_PIN, HIGH/LOW)
      │                            │                           ├─ PATTERN_CONFIG → Fast blink pattern
      │                            │                           ├─ PATTERN_BLE_READY → Slow blink pattern
      │                            │                           ├─ PATTERN_SYNC → Medium blink pattern
      │                            │                           ├─ PATTERN_ERROR → Fast error blink
      │                            │                           ├─ PATTERN_BIKE_ARRIVED → updateBlinkPattern(3 blinks)
      │                            │                           ├─ PATTERN_BIKE_LEFT → Long blink + return to BLE_READY
      │                            │                           └─ PATTERN_COUNT → updateBlinkPattern(N blinks)
      │                            └─ LEDController::updateBlinkPattern()
      ├─ BufferManager::isCriticallyFull()
      ├─ checkPeriodicSync() ──┐
      │                        ├─ millis() - lastSyncCheck
      │                        ├─ BufferManager::needsSync()
      │                        ├─ BikePairing::isSafeToExit()
      │                        └─ changeState(STATE_CLOUD_SYNC)
      ├─ switch(currentState) ──┐
      │                         ├─ STATE_CONFIG_AP → ConfigAP::update()
      │                         ├─ STATE_BIKE_PAIRING → BikePairing::update()
      │                         └─ STATE_CLOUD_SYNC → CloudSync::update()
      ├─ SyncMonitor::shouldFallback()
      └─ printStatus() [a cada 30s]
```

## 🏛️ Estados - changeState()

```
changeState(newState)
  ├─ getStateName(currentState)
  ├─ Exit Current State ──┐
  │                       ├─ STATE_CONFIG_AP → ConfigAP::exit()
  │                       ├─ STATE_BIKE_PAIRING → BikePairing::exit()
  │                       └─ STATE_CLOUD_SYNC → CloudSync::exit()
  ├─ currentState = newState
  ├─ stateStartTime = millis()
  └─ Enter New State ──┐
                       ├─ STATE_CONFIG_AP → ConfigAP::enter(isInitialMode)
                       ├─ STATE_BIKE_PAIRING → BikePairing::enter()
                       └─ STATE_CLOUD_SYNC → CloudSync::enter() → handleSyncResult()
```

## 🔧 Estado CONFIG_AP

```
ConfigAP::enter(isInitialMode)
  ├─ WiFi.mode(WIFI_AP)
  ├─ WiFi.softAP(AP_SSID, AP_PASSWORD)
  ├─ WiFi.onEvent() [Callbacks]
  ├─ ConfigAP::setupWebServer() ──┐
  │                                ├─ server.on("/", HTTP_GET) → HTML form handler
  │                                ├─ server.on("/save", HTTP_POST) ──┐
  │                                │                                   ├─ ConfigManager::getConfig()
  │                                │                                   ├─ strcpy() [múltiplas]
  │                                │                                   ├─ ConfigManager::saveConfig()
  │                                │                                   ├─ ConfigAP::tryUpdateWiFiInFirebase() ──┐
  │                                │                                   │                                         ├─ WiFi.begin(ssid, password)
  │                                │                                   │                                         ├─ HTTPClient::begin(url)
  │                                │                                   │                                         ├─ HTTPClient::PUT(jsonString)
  │                                │                                   │                                         └─ WiFi.softAP() [volta AP]
  │                                │                                   └─ ESP.restart()
  │                                ├─ server.on("/status", HTTP_GET) → Status JSON
  │                                └─ server.on("/save-json", HTTP_POST) → JSON config handler
  ├─ server.begin()
  ├─ apStartTime = millis()
  └─ LEDController::configPattern()

ConfigAP::update()
  ├─ server.handleClient()
  ├─ millis() - apStartTime
  ├─ configManager.getConfig().timeouts.config_ap_min
  └─ if (timeout) → ESP.restart() | return

ConfigAP::exit()
  ├─ server.stop()
  ├─ WiFi.softAPdisconnect(true)
  └─ WiFi.removeEvent()
```

## 🚲 Estado BIKE_PAIRING

```
BikePairing::enter()
  ├─ BikeManager::init() ──┐
  │                        └─ BikeManager::loadData() ──┐
  │                                                     ├─ LittleFS.exists(BIKE_DATA_FILE)
  │                                                     ├─ LittleFS.open(BIKE_DATA_FILE, "r")
  │                                                     ├─ deserializeJson(bikes, file)
  │                                                     └─ dataLoaded = true
  ├─ currentStatus = PAIRING_IDLE
  ├─ lastActivity = millis()
  ├─ BLEServer::start() ──┐
  │                       ├─ NimBLEDevice::init(BLE_DEVICE_NAME)
  │                       ├─ NimBLEDevice::setPower(ESP_PWR_LVL_P3)
  │                       ├─ NimBLEDevice::createServer()
  │                       ├─ pServer->setCallbacks(new ServerCallbacks())
  │                       ├─ pService->createService(BLE_SERVICE_UUID)
  │                       ├─ pDataChar->createCharacteristic(BLE_CHAR_DATA_UUID)
  │                       ├─ pConfigChar->createCharacteristic(BLE_CHAR_CONFIG_UUID)
  │                       ├─ pDataChar->setCallbacks(new DataCallbacks())
  │                       ├─ pConfigChar->setCallbacks(new ConfigCallbacks())
  │                       ├─ pService->start()
  │                       └─ NimBLEDevice::startAdvertising()
  └─ LEDController::bikePairingPattern()

BikePairing::update()
  ├─ BikePairing::processDataQueue() ──┐
  │                                     ├─ millis() - requestTimeout > BIKE_TIMEOUT_MS
  │                                     ├─ BikePairing::finishCurrentBike()
  │                                     └─ BikePairing::requestDataFromBike()
  ├─ millis() - lastHeartbeat > HEARTBEAT_INTERVAL
  ├─ BikePairing::sendHeartbeat() ──┐
  │                                 ├─ DynamicJsonDocument heartbeat(1024)
  │                                 ├─ BikeManager::populateHeartbeatData(bikes)
  │                                 ├─ BLEServer::getConnectedBikes()
  │                                 ├─ BikeManager::getAllowedCount()
  │                                 ├─ BikeManager::getPendingCount()
  │                                 └─ BufferManager::addHeartbeat()
  └─ LEDController::countPattern(connectedBikes) [a cada 30s]

BikePairing::exit()
  ├─ while (!dataQueue.empty()) → dataQueue.pop()
  ├─ currentBike = ""
  ├─ requestTimeout = 0
  ├─ BLEServer::stop()
  └─ currentStatus = PAIRING_IDLE
```

## 📡 Callbacks BLE

```
BLEServer::onBikeConnected(bikeId)
  ├─ LEDController::bikeArrivedPattern()
  ├─ BikeManager::canConnect(bikeId) ──┐
  │                                    ├─ bikeId.startsWith("bpr-") && bikeId.length() == 10
  │                                    ├─ bikes.containsKey(bikeId)
  │                                    ├─ BikeManager::addPendingBike(bikeId) [se nova]
  │                                    └─ status != "blocked"
  ├─ BLEServer::forceDisconnectBike(bikeId) [se blocked]
  ├─ BikeManager::hasConfigUpdate(bikeId)
  ├─ BikeManager::getConfigForBike(bikeId) ──┐
  │                                          ├─ bikes[bikeId]["config"].isNull() → generateDefaultConfig()
  │                                          ├─ response["type"] = "config_push"
  │                                          ├─ response["bike_id"] = bikeId
  │                                          ├─ response["config"] = bikes[bikeId]["config"]
  │                                          └─ serializeJson(response, result)
  ├─ BLEServer::pushConfigToBike(bikeId, config)
  └─ BikeManager::markConfigSent(bikeId)

BLEServer::onBikeDisconnected(bikeId)
  └─ LEDController::bikeLeftPattern()

BLEServer::onBikeDataReceived(bikeId, jsonData)
  ├─ BikeManager::canConnect(bikeId)
  ├─ BikeManager::isAllowed(bikeId) ──┐
  │                                   ├─ bikeId.startsWith("bpr-") && bikeId.length() == 10
  │                                   ├─ bikes.containsKey(bikeId)
  │                                   └─ status == "allowed"
  ├─ BikeManager::recordPendingVisit(bikeId)
  ├─ BikePairing::processDataFromBike(bikeId, jsonData) ──┐
  │                                                       ├─ deserializeJson(doc, jsonData)
  │                                                       ├─ BikeManager::updateHeartbeat(bikeId, battery, heap) ──┐
  │                                                       │                                                        ├─ time(nullptr)
  │                                                       │                                                        ├─ getLocalTime(&timeinfo)
  │                                                       │                                                        ├─ strftime(dateStr, ...)
  │                                                       │                                                        ├─ bikes[bikeId]["last_heartbeat"]["timestamp"] = now
  │                                                       │                                                        ├─ bikes[bikeId]["last_heartbeat"]["battery"] = battery
  │                                                       │                                                        └─ bikes[bikeId]["last_heartbeat"]["heap"] = heap
  │                                                       ├─ BufferManager::addBikeData(bikeId, jsonData) ──┐
  │                                                       │                                                 ├─ deserializeJson(doc, jsonData)
  │                                                       │                                                 ├─ time(nullptr)
  │                                                       │                                                 ├─ getLocalTime(&timeinfo)
  │                                                       │                                                 ├─ strftime(dateStr, ...)
  │                                                       │                                                 ├─ doc["central_receive_timestamp"] = now
  │                                                       │                                                 ├─ doc["central_receive_timestamp_human"] = dateStr
  │                                                       │                                                 ├─ serializeJson(doc, modifiedJson)
  │                                                       │                                                 └─ BufferManager::addData(bikeId, modifiedJson.c_str(), length) ──┐
  │                                                       │                                                                                                                    ├─ CRC32::update(finalData, finalSize)
  │                                                       │                                                                                                                    ├─ buffer[dataCount].bikeId = bikeId
  │                                                       │                                                                                                                    ├─ buffer[dataCount].timestamp = time(nullptr)
  │                                                       │                                                                                                                    ├─ buffer[dataCount].crc32 = checksum
  │                                                       │                                                                                                                    ├─ memcpy(buffer[dataCount].data, finalData, finalSize)
  │                                                       │                                                                                                                    ├─ dataCount++
  │                                                       │                                                                                                                    └─ BufferManager::saveBuffer() [a cada 5 itens]
  │                                                       ├─ BikeManager::hasConfigUpdate(bikeId)
  │                                                       ├─ BLEServer::pushConfigToBike(bikeId, config)
  │                                                       └─ BikePairing::finishCurrentBike()
  └─ BikePairing::enqueueBike(bikeId, jsonData)

BLEServer::onConfigRequest(bikeId, request)
  ├─ deserializeJson(doc, request)
  ├─ type == "config_request" ──┐
  │                             ├─ BikeManager::hasConfigUpdate(bikeId)
  │                             ├─ BikeManager::getConfigForBike(bikeId)
  │                             ├─ BLEServer::pushConfigToBike(bikeId, config)
  │                             └─ BikeManager::markConfigSent(bikeId)
  └─ type == "config_received"
      └─ currentStatus = PAIRING_IDLE
```

## ☁️ Estado CLOUD_SYNC

```
CloudSync::enter()
  ├─ LEDController::syncPattern()
  ├─ CloudSync::connectWiFi() ──┐
  │                             ├─ WiFi.mode(WIFI_STA)
  │                             ├─ WiFi.begin(ssid, password)
  │                             └─ while (WiFi.status() != WL_CONNECTED)
  ├─ CloudSync::syncTime() ──┐
  │                          ├─ configTime(timezone, 0, ntpServer)
  │                          └─ while (!time(nullptr))
  ├─ CloudSync::downloadCentralConfig() ──┐
  │                                       ├─ HTTPClient::begin(configUrl)
  │                                       ├─ HTTPClient::GET()
  │                                       ├─ ConfigManager::updateFromJson(payload) ──┐
  │                                       │                                           ├─ [Múltiplas atribuições de campos do Firebase]
  │                                       │                                           ├─ ConfigManager::saveConfig() ──┐
  │                                       │                                           │                                 ├─ DynamicJsonDocument doc(2048)
  │                                       │                                           │                                 ├─ [Múltiplas atribuições para doc]
  │                                       │                                           │                                 ├─ LittleFS.open(CONFIG_FILE, "w")
  │                                       │                                           │                                 └─ serializeJson(doc, file)
  │                                       │                                           └─ Serial.printf() [logs de atualização]
  │                                       └─ ConfigManager::isValidFirebaseConfig() ──┐
  │                                                                                   ├─ doc["intervals"]["sync_sec"]
  │                                                                                   ├─ doc["timeouts"]["wifi_sec"]
  │                                                                                   ├─ doc["led"]["ble_ready_ms"]
  │                                                                                   ├─ doc["limits"]["max_bikes"]
  │                                                                                   └─ doc["fallback"]["max_failures"]
  ├─ CloudSync::downloadBikeData() ──┐
  │                                   └─ BikeManager::downloadFromFirebase() ──┐
  │                                                                            ├─ HTTPClient::begin(bike_configs_url)
  │                                                                            ├─ HTTPClient::GET()
  │                                                                            ├─ deserializeJson(newConfigs, payload)
  │                                                                            ├─ bikes[bikeId]["config"] = bike.value()
  │                                                                            ├─ configChanged[bikeId] = true [se version mudou]
  │                                                                            └─ BikeManager::saveData()
  ├─ CloudSync::uploadBufferData() ──┐
  │                                  ├─ BufferManager::getDataForUpload(doc) ──┐
  │                                  │                                         ├─ JsonArray items = doc.createNestedArray("items")
  │                                  │                                         ├─ for (int i = 0; i < dataCount; i++)
  │                                  │                                         ├─ JsonObject item = items.createNestedObject()
  │                                  │                                         └─ [Serialização de todos os itens]
  │                                  ├─ HTTPClient::begin(dataUrl)
  │                                  ├─ HTTPClient::POST(jsonString)
  │                                  ├─ BufferManager::markAsConfirmed() [se sucesso] ──┐
  │                                  │                                                  ├─ BufferManager::createBackup()
  │                                  │                                                  ├─ dataCount = 0
  │                                  │                                                  ├─ lastSync = millis()
  │                                  │                                                  └─ BufferManager::saveBuffer()
  │                                  └─ BufferManager::rollbackUpload() [se falha]
  ├─ CloudSync::uploadHeartbeat() ──┐
  │                                 ├─ DynamicJsonDocument heartbeat
  │                                 ├─ HTTPClient::begin(heartbeatUrl)
  │                                 └─ HTTPClient::PUT(jsonString)
  ├─ CloudSync::uploadBikeData()
  ├─ CloudSync::uploadWiFiConfig() [se firstSync]
  └─ return SyncResult::SUCCESS | SyncResult::FAILURE

CloudSync::update()
  ├─ millis() - stateStartTime > timeout
  └─ handleSyncResult(SyncResult::FAILURE)

CloudSync::exit()
  ├─ WiFi.disconnect(true)
  └─ WiFi.mode(WIFI_OFF)
```

## 🔍 Funções Auxiliares

```
handleSyncResult(result)
  ├─ switch(result)
  ├─ SyncResult::SUCCESS ──┐
  │                        ├─ firstSync = false
  │                        └─ changeState(STATE_BIKE_PAIRING)
  └─ SyncResult::FAILURE ──┐
                           ├─ if (firstSync) → changeState(STATE_CONFIG_AP)
                           └─ else → changeState(STATE_BIKE_PAIRING)

printStatus()
  ├─ configManager.getConfig().base_id
  ├─ getStateName(currentState)
  ├─ millis() / 1000 [uptime]
  ├─ if (STATE_CONFIG_AP) ──┐
  │                         └─ Serial.println() [AP info]
  └─ else ──┐
            ├─ BikePairing::getConnectedBikes()
            ├─ ESP.getFreeHeap()
            ├─ configManager.getConfig().sync_interval_ms()
            ├─ (millis() - stateStartTime) / 1000
            └─ Serial.printf() [status completo]

checkPeriodicSync()
  ├─ if (currentState != STATE_BIKE_PAIRING) → return
  ├─ millis() - lastSyncCheck <= sync_interval → return
  ├─ lastSyncCheck = millis()
  ├─ BufferManager::needsSync() ──┐
  │                               ├─ dataCount > 0
  │                               ├─ (dataCount * 100 / maxSize) >= syncThreshold
  │                               └─ millis() - lastSync > autoSaveInterval
  ├─ BikePairing::isSafeToExit() ──┐
  │                                ├─ BikePairing::getStatus() == PAIRING_IDLE
  │                                └─ (millis() - lastActivity) > busyTimeout
  └─ changeState(STATE_CLOUD_SYNC)
```

## 📊 Resumo de Conexões por Arquivo

### main.cpp → Conecta com:
- SelfCheck (systemCheck)
- ConfigManager (loadConfig)
- BufferManager (begin, isCriticallyFull, needsSync)
- LEDController (begin, bootPattern, update)
- ConfigAP (enter, update, exit)
- BikePairing (enter, update, exit, isSafeToExit)
- CloudSync (enter, update, exit)

### config_manager.cpp → Conecta com:
- LittleFS (exists, open)
- ArduinoJson (deserializeJson, serializeJson)

### buffer_manager.cpp → Conecta com:
- LittleFS (exists, open)
- ArduinoJson (deserializeJson, serializeJson)
- CRC32 (update)

### bike_pairing.cpp → Conecta com:
- BikeManager (init, canConnect, isAllowed, updateHeartbeat, hasConfigUpdate, getConfigForBike)
- BLEServer (start, stop, getConnectedBikes, pushConfigToBike)
- BufferManager (addBikeData, addHeartbeat)
- LEDController (bikePairingPattern, bikeArrivedPattern, bikeLeftPattern, countPattern)

### cloud_sync.cpp → Conecta com:
- WiFi (mode, begin, status, disconnect)
- HTTPClient (begin, GET, POST, PUT)
- ConfigManager (updateFromJson, isValidFirebaseConfig)
- BikeManager (downloadFromFirebase)
- BufferManager (getDataForUpload, markAsConfirmed, rollbackUpload)
- LEDController (syncPattern)

### led_controller.cpp → Conecta com:
- digitalWrite, pinMode
- millis()

### bike_manager.cpp → Conecta com:
- LittleFS (exists, open)
- ArduinoJson (deserializeJson, serializeJson)
- HTTPClient (begin, GET)
- time(), getLocalTime()

Este diagrama linear mostra todas as conexões entre arquivos e funções, facilitando a visualização do fluxo completo de execução do firmware.