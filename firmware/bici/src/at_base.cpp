#include "at_base.h"
#include <ArduinoJson.h>
#include "power_manager.h"
#include "batch_proto.h"

extern PowerManager powerManager;
extern AtBaseState* atBaseState;

AtBaseState::AtBaseState(ConfigManager& configMgr, BufferManager& bufferMgr)
    : configManager(configMgr), bufferManager(bufferMgr),
      pClient(nullptr), pDataChar(nullptr), pConfigChar(nullptr),
      bleConnected(false), lastStatusSent(0),
      transferActive(false), awaitingAck(false), fragIndex(0), fragTotal(0),
      fragRetries(0), lastFragmentSend(0), transferComplete(false),
      transferDoneNoClear(false), configRequested(false), busyUntilMs(0),
      announced(false), announceRetries(0), announceSentAt(0),
      statusPagesMask(0), statusPagesTotal(0), statusComplete(false),
      pendingIdx(0),
      visitStart(0), configReceived(false), configRequestTime(0),
      configRetries(0), configGaveUp(false), disconnectSignaled(false) {
    memset(statusBitmap, 0, sizeof(statusBitmap));
    strncpy(statusState, "", sizeof(statusState));
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

    // PRD lotes/recibos: o announce é refeito a cada conexão — a central
    // responde com as pendências ATUAIS ou com o recibo (T11)
    announced = false;
    announceRetries = 0;
    announceSentAt = 0;
    statusPagesMask = 0;
    statusPagesTotal = 0;
    statusComplete = false;
    strncpy(statusState, "", sizeof(statusState));
    pendingParts.clear();
    pendingIdx = 0;
    transferActive = false;
    transferComplete = false;
    transferDoneNoClear = false;
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
    Serial.printf("📤 Part %d sent (%d bytes): %s\n",
                  index, fragment.length(), ok ? "OK" : "FAILED");
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

// ---- PRD lotes/recibos: anúncio, pendências e autorização ----------------

bool AtBaseState::sendBatchAnnounce() {
    if (!pClient || !bleConnected || !pDataChar) return false;

    char shaHex[65];
    bufferManager.getBatchSha256Hex(shaHex);

    DynamicJsonDocument doc(384);
    doc["type"] = "batch_announce";
    doc["v"] = BATCH_PROTO_VERSION;
    doc["bike_id"] = bufferManager.getCurrentSession().bike_id;
    doc["session_id"] = bufferManager.getSessionId();
    doc["session_salt"] = bufferManager.getSessionSalt();
    doc["total"] = bufferManager.getBatchTotalParts();
    doc["total_bytes"] = bufferManager.getBatchTotalBytes();
    doc["sha256"] = shaHex;

    String out;
    serializeJson(doc, out);
    if (out.length() > 512) {
        Serial.println("❌ batch_announce excede o limite de mensagem");
        return false;
    }
    bool ok = pDataChar->writeValue(out.c_str());
    Serial.printf("📢 Batch announce: %d partes, %lu bytes, sha %s (%s)\n",
                  bufferManager.getBatchTotalParts(),
                  (unsigned long)bufferManager.getBatchTotalBytes(),
                  shaHex, ok ? "OK" : "FAILED");
    return ok;
}

void AtBaseState::sendBatchCleared(uint32_t sid, uint16_t salt, const char shaHex[65]) {
    if (!pClient || !bleConnected || !pDataChar) return;

    DynamicJsonDocument doc(256);
    doc["type"] = "batch_cleared";
    doc["v"] = BATCH_PROTO_VERSION;
    doc["bike_id"] = configManager.getConfig().bike_id;
    doc["session_id"] = sid;
    doc["session_salt"] = salt;
    doc["sha256"] = shaHex;

    String out;
    serializeJson(doc, out);
    pDataChar->writeValue(out.c_str());
}

// Bitmap de pendências em páginas (T16): 64 partes por página, 16 hex chars
void AtBaseState::handleBatchStatus(JsonObject doc) {
    uint32_t sid = doc["session_id"] | 0;
    uint16_t salt = doc["session_salt"] | 0;
    if (sid != bufferManager.getSessionId() || salt != bufferManager.getSessionSalt()) {
        Serial.println("ℹ️ batch_status de outra sessão - ignorado");
        return;
    }

    uint8_t page = doc["page"] | 0;
    uint8_t pages = doc["pages"] | 0;
    if (pages == 0 || pages > BATCH_STATUS_MAX_PAGES || page >= pages) {
        Serial.printf("🚨 batch_status com páginas inválidas (%d/%d) - ignorado\n", page, pages);
        return;
    }

    statusPagesTotal = pages;
    statusPagesMask |= (uint8_t)(1u << (page % 8));
    statusComplete = (statusPagesMask & ((1u << pages) - 1u)) ==
                     (uint8_t)((1u << pages) - 1u);

    // missing: 16 hex chars = 8 bytes = 64 partes desta página
    // bit 1 = parte PRESENTE (a central informa o que já tem)
    String missing = doc["missing"] | "";
    for (int i = 0; i < 16 && i < (int)missing.length(); i++) {
        char c = missing[i];
        uint8_t v = (c >= '0' && c <= '9') ? (c - '0') :
                    (c >= 'a' && c <= 'f') ? (c - 'a' + 10) :
                    (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : 0;
        statusBitmap[page * 8 + i / 2] |= (i % 2 == 0) ? (v >> 4) : (v & 0xF);
    }

    String state = doc["state"] | "";
    strncpy(statusState, state.c_str(), sizeof(statusState) - 1);
    statusState[sizeof(statusState) - 1] = '\0';

    if (statusComplete) {
        Serial.printf("📥 Status completo: %s (%d páginas)\n", statusState, pages);
    }
}

void AtBaseState::handleClearAuthorization(JsonObject doc) {
    uint32_t sid = doc["session_id"] | 0;
    uint16_t salt = doc["session_salt"] | 0;
    String shaHex = doc["sha256"] | "";
    uint16_t total = doc["total"] | 0;
    uint32_t totalBytes = doc["total_bytes"] | 0;

    // T12: lote já limpo — a autorização repetida é respondida de novo,
    // sem recriar a entrega nem executar limpeza adicional
    if (bufferManager.isBatchCleared(sid, salt, shaHex.c_str())) {
        Serial.println("♻️ Autorização de lote JÁ limpo - respondendo 'apaguei' de novo");
        sendBatchCleared(sid, salt, shaHex.c_str());
        transferComplete = true;
        transferActive = false;
        return;
    }

    // Lote atual: valida identidade/total/bytes/hash e persiste ANTES de limpar
    if (sid != bufferManager.getSessionId() || salt != bufferManager.getSessionSalt()) {
        Serial.println("🚨 Clear auth de sessão divergente - ignorada");
        return;
    }
    if (!bufferManager.applyClearAuthorization(shaHex.c_str(), total, totalBytes)) {
        // Autorização inválida: dados preservados (PRD 5.3 — não inferir)
        Serial.println("🚨 Clear auth inválida para o lote atual - dados preservados");
        return;
    }

    // Executa a limpeza cirúrgica e responde (PRD 5.2: autorização persistida
    // → remover cobertura → persistir estado final → responder)
    if (bufferManager.performClearIfAuthorized()) {
        sendBatchCleared(sid, salt, shaHex.c_str());
        transferComplete = true;
        transferActive = false;
        Serial.println("🔓 Lote autorizado, limpo e confirmado à central");
    } else {
        Serial.println("🚨 Limpeza não executou (estado inconsistente) - manterá dados");
    }
}

void AtBaseState::startPartsTransfer() {
    // Monta a lista de pendências do bitmap acumulado (bit 0 = falta)
    pendingParts.clear();
    uint16_t total = bufferManager.getBatchTotalParts();
    for (uint16_t i = 0; i < total && i < BATCH_PROTO_MAX_PARTS; i++) {
        if ((statusBitmap[i / 8] & (1u << (i % 8))) == 0) {
            pendingParts.push_back(i);
        }
    }
    pendingIdx = 0;
    fragTotal = (uint16_t)pendingParts.size();
    fragIndex = 0;
    fragRetries = 0;
    awaitingAck = false;

    if (fragTotal == 0) {
        // A central já tem tudo persistido: aguardando nuvem (T08/T09) —
        // a bike MANTÉM a cópia até a autorização de limpeza
        Serial.println("ℹ️ Central já tem todas as partes - aguardando confirmação da nuvem");
        transferDoneNoClear = true;
        transferActive = false;
        return;
    }

    transferActive = true;
    Serial.printf("🚀 Enviando %d partes pendentes (de %d no lote)\n", fragTotal, total);
}

void AtBaseState::startTransfer() {
    // PRD lotes: a entrega começa com o ANÚNCIO — o lote é fechado ANTES de
    // transmitir (identidade/total/bytes/hash persistidos; PRD 4.1)
    if (!bufferManager.hasBatchToDeliver()) {
        return;
    }
    if (!bufferManager.freezeBatch()) {
        Serial.println("🚨 Falha ao fechar o lote - entrega adiada (dados preservados)");
        return;
    }
    // O announce é enviado pelo update() (controla retry/timeout aqui)
}

void AtBaseState::abortTransfer() {
    if (transferActive) {
        Serial.println("⏸️ Transfer paused - data kept until clear authorization");
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

    // Processa ACKs/configs/status recebidos (fila da task BLE)
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
                // X08: desiste desta conexão — transferência prossegue com a
                // config atual; reconexão futura pede de novo
                configGaveUp = true;
                Serial.println("ℹ️ Config não veio - seguindo com a config atual");
            }
        }
    }

    // Heartbeat periódico (mesmo sem dados)
    if (millis() - lastStatusSent > HEARTBEAT_INTERVAL_MS) {
        sendStatus();
    }

    // ---- PRD lotes/recibos: fluxo announce → pendências → partes → autorização

    // T16: o MTU negociado precisa comportar as partes (460+3) e o anúncio;
    // MTU pequeno = recusa explícita com dados preservados, não entrega corrompida
    if (!bufferManager.isEmpty() && pClient->getMTU() < MAX_BLE_FRAGMENT_BYTES + 3 + 8) {
        Serial.printf("🚨 MTU negociado %d não comporta o protocolo de lotes - dados preservados\n",
                      pClient->getMTU());
        abortTransfer();
        pClient->disconnect();
        return STATE_SCANNING;
    }

    // 1) Lote pendente: fechar (se preciso) e anunciar
    if (!announced && !transferComplete && !transferDoneNoClear) {
        if (!bufferManager.hasBatchToDeliver() && !bufferManager.isBatchFrozen()) {
            if (bufferManager.isEmpty()) {
                return STATE_AT_BASE; // nada a entregar nesta visita
            }
            startTransfer(); // fecha o lote (freeze) — announce no próximo ciclo
            if (!bufferManager.isBatchFrozen()) {
                return STATE_AT_BASE; // freeze falhou; diagnóstico já emitido
            }
        }
        if (bufferManager.hasBatchToDeliver()) {
            if (sendBatchAnnounce()) {
                announced = true;
                announceSentAt = millis();
                announceRetries = 0;
            } else {
                announceRetries++;
                if (announceRetries >= MAX_BATCH_ANNOUNCE_RETRIES) {
                    Serial.println("🚨 Announce não foi escrito - dados preservados, tentarei depois");
                    abortTransfer();
                    pClient->disconnect();
                    return STATE_SCANNING;
                }
            }
        }
        return STATE_AT_BASE;
    }

    // 2) Aguardando batch_status (pendências ou recibo)
    if (announced && !statusComplete && !transferActive && !transferComplete) {
        if (millis() - announceSentAt > BATCH_ANNOUNCE_TIMEOUT_MS) {
            if (announceRetries < MAX_BATCH_ANNOUNCE_RETRIES) {
                announceRetries++;
                Serial.printf("⏰ Sem batch_status - reanunciando (%d/%d)\n",
                              announceRetries, MAX_BATCH_ANNOUNCE_RETRIES);
                if (sendBatchAnnounce()) {
                    announceSentAt = millis();
                }
            } else {
                // T20: central sem o protocolo novo (ou sem resposta) —
                // diagnóstico explícito, dados preservados, NENHUMA limpeza
                Serial.println("🚨 Central não respondeu ao announce (incompatível?) - dados preservados");
                abortTransfer();
                pClient->disconnect();
                return STATE_SCANNING;
            }
        }
        return STATE_AT_BASE;
    }

    // 3) Status completo: decidir pela pendência informada
    if (statusComplete && !transferActive && !transferComplete && !transferDoneNoClear) {
        String state(statusState);
        if (state == "receiving") {
            startPartsTransfer(); // envia só as partes faltantes
        } else if (state == "hash_mismatch") {
            // T05: checksums locais ok, hash divergente — lote pendente para
            // diagnóstico na central; a bike preserva tudo
            Serial.println("🚨 Central reporta hash divergente - lote pendente lá; dados preservados");
            transferDoneNoClear = true;
        } else {
            // complete_local / awaiting_cloud: a central tem tudo; a limpeza
            // depende da confirmação da NUVEM (T08/T09) — manter a cópia
            Serial.printf("ℹ️ Lote %s na central - aguardando confirmação da nuvem para limpar\n",
                          statusState);
            transferDoneNoClear = true;
        }
    }

    // 4) Envio das partes pendentes (uma por vez, ACK da aplicação)
    if (transferActive) {
        // R13: busy impõe espera real antes de nova tentativa
        // (comparação por diferença sinalizada — segura no wrap de millis)
        if (busyUntilMs != 0 && (int32_t)(busyUntilMs - millis()) > 0) {
            return STATE_AT_BASE;
        }

        if (!awaitingAck) {
            if (pendingIdx >= pendingParts.size()) {
                // Todas as pendências foram ACKeadas — aguardar a conclusão
                // (hash/nuvem) que virá como novo status/autorização
                Serial.println("ℹ️ Todas as partes pendentes enviadas - aguardando conferência");
                transferActive = false;
                return STATE_AT_BASE;
            }
            uint16_t part = pendingParts[pendingIdx];
            if (sendFragment(part)) {
                // R13: sucesso de ESCRITA não é ACK da aplicação — o contador
                // de retries só zera quando o ACK da parte chega
                awaitingAck = true;
                lastFragmentSend = millis();
            } else {
                fragRetries++;
                if (fragRetries >= MAX_FRAGMENT_RETRIES) {
                    Serial.println("❌ Part write failed repeatedly - aborting");
                    abortTransfer();
                    pClient->disconnect();
                    return STATE_SCANNING;
                }
            }
        } else if (millis() - lastFragmentSend > FRAGMENT_ACK_TIMEOUT_MS) {
            // ACK não chegou: reenvia (idempotente na central por índice+CRC)
            fragRetries++;
            Serial.printf("⏰ ACK timeout for part %d (retry %d/%d)\n",
                          pendingParts[pendingIdx], fragRetries, MAX_FRAGMENT_RETRIES);
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

    // 5) Lote limpo e confirmado — encerra a visita; a sessão renasce (com
    // as observações pós-lote, se houver) ou uma nova será aberta no boot
    if (transferComplete) {
        Serial.println("✅ Batch cleared and confirmed - leaving base");
        transferComplete = false;
        pClient->disconnect();
        return STATE_SCANNING;
    }

    // 6) Concluído sem autorização (aguardando nuvem): encerra a visita
    // mantendo os dados — a autorização virá numa visita futura (T09/T11)
    if (transferDoneNoClear) {
        Serial.println("ℹ️ Visita concluída sem autorização de limpeza - dados mantidos");
        transferDoneNoClear = false;
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
    // F12: cliente mantido para reuso; PRD lotes: dados permanecem até a
    // autorização de limpeza persistida — o lote congelado também permanece
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

    if (type == "batch_status") {
        handleBatchStatus(doc.as<JsonObject>());
        return;
    }

    if (type == "batch_clear_auth") {
        handleClearAuthorization(doc.as<JsonObject>());
        return;
    }

    if (type == "data_ack") {
        uint32_t sessionId = doc["session_id"] | 0;
        uint16_t sessionSalt = doc["session_salt"] | 0;
        uint16_t seq = doc["seq"] | 0;

        if (!transferActive || sessionId != bufferManager.getSessionId() ||
            sessionSalt != bufferManager.getSessionSalt()) {
            Serial.println("⚠️ ACK for unknown session - ignored");
            return;
        }

        if (doc["conflict"] | false) {
            // T03: mesmo índice com conteúdo diferente = conflito explícito
            Serial.printf("🚨 Central reporta CONFLITO na parte %d - lote pendente de diagnóstico\n", seq);
            abortTransfer();
            return;
        }

        if (!(doc["stored"] | false)) {
            Serial.printf("❌ Central could not store part %d\n", seq);
            abortTransfer();
            return;
        }

        // A parte em envio é pendingParts[pendingIdx]; ACK de outra parte
        // (reenvio antigo duplicado) é tolerado e não move o cursor
        if (pendingIdx < pendingParts.size() && seq == pendingParts[pendingIdx]) {
            awaitingAck = false;
            // R13: contador por parte zera apenas com ACK da aplicação
            fragRetries = 0;
            pendingIdx++;
            Serial.printf("✅ ACK for part %d (%lu/%d pendências)\n",
                          seq, (unsigned long)pendingIdx, fragTotal);
        } else {
            Serial.printf("ℹ️ ACK duplicado/fora de ordem da parte %d - ignorado\n", seq);
        }
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
