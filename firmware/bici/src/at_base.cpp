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
      configRequested(false), busyUntilMs(0),
      lastSessionId(0), lastSessionSalt(0),
      visitStart(0), configReceived(false), configRequestTime(0),
      configRetries(0), configGaveUp(false), disconnectSignaled(false) {
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
    // X02: cada conexão abre uma visita com orçamento global próprio
    visitStart = millis();
    configReceived = false;
    configRequestTime = 0;
    configRetries = 0;
    configGaveUp = false;
    return true;
}

void AtBaseState::sendStatus() {
    if (!pClient || !bleConnected || !pDataChar) return;

    BikeConfig& config = configManager.getConfig();
    bool hasData = !bufferManager.isEmpty();
    // N02: carga útil real pendente (não a capacidade constante da struct)
    String json = powerManager.getHeartbeatReport(
        config, hasData, bufferManager.getCurrentSession().scan_count,
        bufferManager.getPendingDataBytes());

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

    // X02: retomada — mesma sessão inalterada (id/salt) continua do primeiro
    // fragmento SEM ACK em vez de recomeçar do zero a cada reconexão/busy;
    // a central deduplica reenvios por (session_id, salt, seq, CRC)
    bool sameSession = (bufferManager.getSessionId() == lastSessionId &&
                        bufferManager.getSessionSalt() == lastSessionSalt &&
                        fragIndex > 0 && fragIndex < fragTotal);
    if (!sameSession) {
        fragIndex = 0;
    }
    lastSessionId = bufferManager.getSessionId();
    lastSessionSalt = bufferManager.getSessionSalt();

    transferActive = true;
    transferComplete = false;
    fragRetries = 0;
    awaitingAck = false;
    Serial.printf("🚀 Starting transfer: %d fragments (from %d)\n", fragTotal, fragIndex);
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
    // X09: desconexão sinalizada pela task NimBLE é processada AQUI, no
    // contexto do loop — o callback não zera ponteiros/flags compartilhados
    // enquanto o loop pode estar dentro de sendFragment()
    if (disconnectSignaled) {
        disconnectSignaled = false;
        onBLEDisconnected();
        return STATE_SCANNING;
    }

    if (!bleConnected) {
        return STATE_SCANNING;
    }

    // Check connection
    if (!pClient || !pClient->isConnected()) {
        onBLEDisconnected();
        return STATE_SCANNING;
    }

    // X02: orçamento GLOBAL da visita — idle sem dados, busy persistente ou
    // rejeição contínua encerram a visita em prazo finito; a proteção de
    // bateria do loop nunca fica suspensa por mais que isso
    if (millis() - visitStart > AT_BASE_MAX_DURATION_MS) {
        Serial.println("⏰ AT_BASE visit budget exhausted - disconnecting");
        abortTransfer();
        pClient->disconnect();
        return STATE_SCANNING;
    }

    // Throttle de log: "Syncing data" a cada 5 s, não a cada passada do loop
    static uint32_t lastSyncLog = 0;
    if (millis() - lastSyncLog > 5000) {
        Serial.println("🏠 AT_BASE - Syncing data");
        lastSyncLog = millis();
    }

    // Processa ACKs/configs recebidos (fila da task BLE)
    processPendingNotifications();

    // Identificação + pedido de config na chegada
    if (!configRequested) {
        sendStatus();
        if (requestConfig()) {
            configRequestTime = millis();
        } else {
            // X08: escrita falhou — repete na próxima iteração (não marca como pedido)
            Serial.println("⚠️ Config request write failed - will retry");
        }
        configRequested = true;
    } else if (!configReceived && !configGaveUp) {
        // X08: sem config dentro do prazo, repete o pedido (a central pode
        // ter perdido a escrita, a notificação ou recusado por MTU)
        if (millis() - configRequestTime > CONFIG_REQUEST_TIMEOUT_MS) {
            if (configRetries < MAX_CONFIG_REQUEST_RETRIES) {
                configRetries++;
                Serial.printf("📡 Config not received - retrying (%d/%d)\n",
                              configRetries, MAX_CONFIG_REQUEST_RETRIES);
                if (requestConfig()) {
                    configRequestTime = millis();
                }
            } else {
                // X08: desiste DESTA conexão — transferência prossegue com a
                // config atual; uma reconexão futura pede de novo
                configGaveUp = true;
                Serial.println("⚠️ Config not received after retries - continuing with current config");
            }
        }
    }

    // Heartbeat periódico (mesmo sem dados)
    if (millis() - lastStatusSent > HEARTBEAT_INTERVAL_MS) {
        sendStatus();
    }

    // R13/X02: busy impõe espera real ANTES de (re)abrir a transferência —
    // durante a espera transferActive permanece false e a proteção de
    // bateria do loop continua ativa (comparação segura no wrap de millis)
    if (busyUntilMs != 0 && (int32_t)(busyUntilMs - millis()) > 0) {
        return STATE_AT_BASE;
    }

    // F05: transferir dados apenas com confirmação de persistência por fragmento
    if (!transferActive && !transferComplete && !bufferManager.isEmpty()) {
        startTransfer();
    }

    if (transferActive) {
        if (!awaitingAck) {
            if (sendFragment(fragIndex)) {
                // R13: sucesso de ESCRITA não é ACK da aplicação — o contador
                // de retries só zera quando o ACK do fragmento chega
                awaitingAck = true;
                lastFragmentSend = millis();
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
                // R13: ausência persistente de ACK encerra a tentativa
                Serial.println("❌ No ACK after repeated retries - aborting transfer");
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
    // X09: chamado SOMENTE no contexto do loop (via update/sinal) — a task
    // do NimBLE apenas seta disconnectSignaled
    Serial.println("🔴 BLE disconnected");
    disconnectSignaled = false;
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
        // R13: contador por fragmento zera apenas com ACK da aplicação
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
        // X08: config chegou — encerra o ciclo de retry do pedido
        configReceived = true;
        bool applied = configManager.processUpdate(message);
        sendConfigAck(applied, configManager.getConfig().version);
        return;
    }

    if (type == "busy") {
        // R13: respeita retry_after_sec em vez de reiniciar imediatamente
        uint32_t retryAfterSec = doc["retry_after_sec"] | 30;
        if (retryAfterSec > 3600) retryAfterSec = 3600; // X02: teto da espera
        busyUntilMs = millis() + retryAfterSec * 1000UL;
        Serial.printf("⏸️ Central busy - pausing transfer for %lus\n", (unsigned long)retryAfterSec);
        if (transferActive) {
            abortTransfer();
        }
        return;
    }

    // R13/F05: a rota legada "proceed" (limpeza sem sessão/seq/ACK final) foi
    // removida — limpeza somente via data_ack final com can_clear_buffer

    Serial.printf("ℹ️ Unhandled notification type: %s\n", type.c_str());
}

bool AtBaseState::isConnected() const {
    return bleConnected && pClient && pClient->isConnected();
}

void ClientCallbacks::onDisconnect(NimBLEClient* pClient) {
    // X09: a task do NimBLE apenas SINALIZA — zerar ponteiros/flags aqui
    // concorria com o loop usando pDataChar dentro de sendFragment()
    if (atBaseState) {
        atBaseState->signalDisconnected();
    }
}
