#include "at_base.h"
#include <ArduinoJson.h>
#include "power_manager.h"

extern PowerManager powerManager;
extern AtBaseState* atBaseState;

AtBaseState::AtBaseState(ConfigManager& configMgr, BufferManager& bufferMgr)
    : configManager(configMgr), bufferManager(bufferMgr),
      pClient(nullptr), pDataChar(nullptr), pConfigChar(nullptr),
      bleConnected(false), lastStatusSent(0),
      transferActive(false), awaitingAck(false), fragIndex(0), fragTotal(0),
      fragRetries(0), lastFragmentSend(0), transferComplete(false),
      configRequested(false) {
    notifQueue = xQueueCreate(4, NOTIF_MAX_LEN);
}

// F24: aceita apenas centrais prontas (nome base sem BUSY/OFF)
static bool isCentralAvailable(const std::string& name, const char* baseName) {
    if (name.empty()) return false;
    if (name.find(baseName) != 0) return false;
    if (name.find("BUSY") != std::string::npos) return false;
    if (name.find("OFF") != std::string::npos) return false;
    return true;
}

bool AtBaseState::scanForBase() {
    BikeConfig& config = configManager.getConfig();
    Serial.printf("🔍 Scanning for BLE base '%s*' (timeout: %ds)...\n",
                  config.ble.base_name, config.ble.scan_time_sec);

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    NimBLEScanResults results = pScan->start(config.ble.scan_time_sec, false);

    bool found = false;
    for (int i = 0; i < results.getCount() && !found; i++) {
        NimBLEAdvertisedDevice device = results.getDevice(i);

        if (isCentralAvailable(device.getName(), config.ble.base_name)) {
            Serial.printf("🔍 Found available base: %s\n", device.getName().c_str());

            if (connectToBase(&device)) {
                found = true;
            }
        }
    }

    pScan->clearResults();
    if (!found) {
        Serial.println("❌ No available BLE base found");
    }
    return found;
}

bool AtBaseState::connectToBase(NimBLEAdvertisedDevice* device) {
    BikeConfig& config = configManager.getConfig();

    // F12: reutiliza o cliente em vez de criar um novo a cada tentativa
    if (!pClient) {
        pClient = NimBLEDevice::createClient();
        if (!pClient) {
            Serial.println("❌ Failed to create BLE client");
            return false;
        }
        pClient->setClientCallbacks(new ClientCallbacks(this), false);
    }

    Serial.printf("🔗 Attempting BLE connection (timeout: %dms)...\n", config.ble.connection_timeout_ms);
    pClient->setConnectTimeout(config.ble.connection_timeout_ms / 1000);

    if (!pClient->connect(device)) {
        Serial.println("❌ BLE connection failed");
        return false;
    }

    Serial.println("✅ BLE connection established");

    NimBLERemoteService* pService = pClient->getService(BLE_SERVICE_UUID);
    if (!pService) {
        Serial.println("❌ Service not found");
        pClient->disconnect();
        return false;
    }

    pDataChar = pService->getCharacteristic(BLE_CHAR_DATA_UUID);
    pConfigChar = pService->getCharacteristic(BLE_CHAR_CONFIG_UUID);

    if (!pDataChar || !pConfigChar) {
        Serial.println("❌ Required characteristics not found");
        pClient->disconnect();
        pDataChar = nullptr;
        pConfigChar = nullptr;
        return false;
    }

    // F06: assina notificações de configuração/resposta antes de qualquer handshake.
    // O callback apenas copia para a fila — parsing/aplicação roda no loop().
    if (!pConfigChar->subscribe(true,
            [this](NimBLERemoteCharacteristic*, uint8_t* pData, size_t length, bool isNotify) {
                if (!isNotify || pData == nullptr || length == 0 || length > 512) return;
                char buffer[NOTIF_MAX_LEN] = {0};
                memcpy(buffer, pData, length);
                if (this->notifQueue) {
                    xQueueSend(this->notifQueue, buffer, 0);
                }
            }, true)) {
        Serial.println("⚠️ Failed to subscribe to config notifications");
    } else {
        Serial.println("📡 Subscribed to config characteristic");
    }

    bleConnected = true;
    lastStatusSent = 0;
    configRequested = false;
    return true;
}

void AtBaseState::sendStatus() {
    if (!pClient || !bleConnected || !pDataChar) return;

    BikeConfig& config = configManager.getConfig();
    bool hasData = !bufferManager.isEmpty();
    String json = powerManager.getHeartbeatReport(
        config, hasData, bufferManager.getCurrentSession().scan_count, bufferManager.getSize());

    if (pDataChar->writeValue(json.c_str())) {
        lastStatusSent = millis();
        Serial.printf("📤 Heartbeat: %s\n", json.c_str());
    } else {
        Serial.println("⚠️ Heartbeat write failed - will retry");
        lastStatusSent = millis() - HEARTBEAT_INTERVAL_MS / 2; // retry em metade do intervalo
    }
}

bool AtBaseState::sendFragment(uint16_t index) {
    if (!pClient || !bleConnected || !pDataChar) return false;

    String fragment;
    if (!bufferManager.serializeFragment(index, fragment)) {
        Serial.printf("❌ Failed to serialize fragment %d\n", index);
        return false;
    }

    bool ok = pDataChar->writeValue(fragment.c_str());
    Serial.printf("📤 Fragment %d/%d sent (%d bytes): %s\n",
                  index + 1, fragTotal, fragment.length(), ok ? "OK" : "FAILED");
    return ok;
}

void AtBaseState::sendConfigAck(bool success, uint32_t version) {
    if (!pClient || !bleConnected || !pConfigChar) return;

    DynamicJsonDocument ack(192);
    ack["type"] = "config_ack";
    ack["bike_id"] = configManager.getConfig().bike_id;
    ack["status"] = success ? "ok" : "error";
    ack["applied_version"] = version;

    String out;
    serializeJson(ack, out);
    pConfigChar->writeValue(out.c_str());
}

void AtBaseState::startTransfer() {
    fragTotal = bufferManager.fragmentCount();
    if (fragTotal == 0) {
        return;
    }
    transferActive = true;
    transferComplete = false;
    fragIndex = 0;
    fragRetries = 0;
    awaitingAck = false;
    Serial.printf("🚀 Starting transfer: %d fragments\n", fragTotal);
}

void AtBaseState::abortTransfer() {
    if (transferActive) {
        Serial.println("⏸️ Transfer paused - data kept until ACK");
    }
    transferActive = false;
    awaitingAck = false;
    fragRetries = 0;
}

bool AtBaseState::requestConfig() {
    if (!pClient || !bleConnected || !pConfigChar) {
        return false;
    }

    BikeConfig& config = configManager.getConfig();
    Serial.println("📡 Requesting configuration from base...");

    DynamicJsonDocument request(256);
    request["type"] = "config_request";
    request["bike_id"] = config.bike_id;
    request["config_version"] = config.version;

    String requestStr;
    serializeJson(request, requestStr);

    return pConfigChar->writeValue(requestStr.c_str());
}

void AtBaseState::processPendingNotifications() {
    if (!notifQueue) return;
    char buffer[NOTIF_MAX_LEN];
    while (xQueueReceive(notifQueue, buffer, 0) == pdTRUE) {
        String message(buffer);
        onNotification(message);
        memset(buffer, 0, sizeof(buffer));
    }
}

BikeState AtBaseState::update() {
    if (!bleConnected) {
        return STATE_SCANNING;
    }

    Serial.println("🏠 AT_BASE - Syncing data");

    // Check connection
    if (!pClient || !pClient->isConnected()) {
        onBLEDisconnected();
        return STATE_SCANNING;
    }

    // Processa ACKs/configs recebidos (fila da task BLE)
    processPendingNotifications();

    // Identificação + pedido de config na chegada
    if (!configRequested) {
        sendStatus();
        requestConfig();
        configRequested = true;
    }

    // Heartbeat periódico (mesmo sem dados)
    if (millis() - lastStatusSent > HEARTBEAT_INTERVAL_MS) {
        sendStatus();
    }

    // F05: transferir dados apenas com confirmação de persistência por fragmento
    if (!transferActive && !transferComplete && !bufferManager.isEmpty()) {
        startTransfer();
    }

    if (transferActive) {
        if (!awaitingAck) {
            if (sendFragment(fragIndex)) {
                awaitingAck = true;
                lastFragmentSend = millis();
                fragRetries = 0;
            } else {
                fragRetries++;
                if (fragRetries >= MAX_FRAGMENT_RETRIES) {
                    Serial.println("❌ Fragment write failed repeatedly - aborting");
                    abortTransfer();
                    pClient->disconnect();
                    return STATE_SCANNING;
                }
            }
        } else if (millis() - lastFragmentSend > FRAGMENT_ACK_TIMEOUT_MS) {
            // ACK não chegou: reenvia (idempotente na central via session_id+seq)
            fragRetries++;
            Serial.printf("⏰ ACK timeout for fragment %d (retry %d/%d)\n",
                          fragIndex, fragRetries, MAX_FRAGMENT_RETRIES);
            if (fragRetries >= MAX_FRAGMENT_RETRIES) {
                abortTransfer();
                pClient->disconnect();
                return STATE_SCANNING;
            }
            awaitingAck = false;
        }
    }

    // Transferência concluída (confirmada pela central): limpa e abre nova sessão
    if (transferComplete) {
        Serial.println("✅ Session confirmed by central - clearing and starting new session");
        bufferManager.clear();
        bufferManager.startSession(configManager.getConfig().bike_id);
        transferComplete = false;
        pClient->disconnect();
        return STATE_SCANNING;
    }

    return STATE_AT_BASE;
}

void AtBaseState::onBLEDisconnected() {
    Serial.println("🔴 BLE disconnected");
    bleConnected = false;
    pDataChar = nullptr;
    pConfigChar = nullptr;
    configRequested = false;
    // F12: cliente mantido para reuso; F05: dados permanecem até ACK
    if (transferActive) {
        abortTransfer();
    }
}

void AtBaseState::onNotification(const String& message) {
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, message) != DeserializationError::Ok) {
        Serial.println("⚠️ Invalid notification JSON");
        return;
    }

    // F11: notificação da central alcança todos os assinantes — só processa
    // mensagens endereçadas a ESTA bike
    if (!doc["bike_id"].isNull()) {
        String target = doc["bike_id"];
        if (target != configManager.getConfig().bike_id) {
            Serial.println("ℹ️ Notification addressed to another bike - ignored");
            return;
        }
    }

    String type = doc["type"] | "";

    if (type == "data_ack") {
        uint32_t sessionId = doc["session_id"] | 0;
        uint16_t sessionSalt = doc["session_salt"] | 0;
        uint16_t seq = doc["seq"] | 0;

        if (!transferActive || sessionId != bufferManager.getSessionId() ||
            sessionSalt != bufferManager.getSessionSalt()) {
            Serial.println("⚠️ ACK for unknown session - ignored");
            return;
        }

        if (!(doc["stored"] | false)) {
            Serial.printf("❌ Central could not store fragment %d\n", seq);
            abortTransfer();
            return;
        }

        if (seq != fragIndex) {
            Serial.printf("⚠️ ACK seq mismatch (%d != %d) - ignored\n", seq, fragIndex);
            return;
        }

        awaitingAck = false;
        fragRetries = 0;
        Serial.printf("✅ ACK for fragment %d/%d\n", seq + 1, fragTotal);

        if (doc["is_final"] | false) {
            if (doc["can_clear_buffer"] | false) {
                transferComplete = true;
                transferActive = false;
            } else {
                // Central persistiu mas ainda há pendência dela — manter dados
                Serial.println("⚠️ Final fragment stored but cannot clear yet");
                transferActive = false;
            }
            return;
        }

        fragIndex++;
        return;
    }

    if (type == "config_push") {
        Serial.println("⚙️ Config update received");
        bool applied = configManager.processUpdate(message);
        sendConfigAck(applied, configManager.getConfig().version);
        return;
    }

    if (type == "busy") {
        Serial.println("⏸️ Central busy - pausing transfer");
        if (transferActive) {
            abortTransfer();
        }
        return;
    }

    if (type == "proceed") {
        // Compatibilidade: permissão de limpeza direta
        if (doc["can_clear_buffer"] | false) {
            transferComplete = true;
            transferActive = false;
        }
        return;
    }

    Serial.printf("ℹ️ Unhandled notification type: %s\n", type.c_str());
}

bool AtBaseState::isConnected() const {
    return bleConnected && pClient && pClient->isConnected();
}

void ClientCallbacks::onDisconnect(NimBLEClient* pClient) {
    if (atBaseState) {
        atBaseState->onBLEDisconnected();
    }
}
