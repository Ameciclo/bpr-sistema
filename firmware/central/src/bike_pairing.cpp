#include <ArduinoJson.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <queue>
#include <vector>
#include "bike_manager.h"
#include "bike_pairing.h"
#include "ble_server.h"
#include "bpr_json_helper.h"
#include "buffer_manager.h"
#include "batch_spool.h"
#include "config_credentials.h"
#include "config_manager.h"
#include "constants.h"
#include "time_sync.h"

extern BufferManager bufferManager;
extern ConfigCredentials configCredentials;
extern SystemState currentState;
extern uint32_t stateStartTime;
extern ConfigManager configManager;

// Estado do pairing com struct organizada
struct PairingState
{
    PairingStatus status;
    uint32_t lastActivity;
    String currentBike;
    uint32_t requestTimeout;

    PairingState() : status(PAIRING_IDLE), lastActivity(0), currentBike(""), requestTimeout(0) {}

    bool isTimedOut() const
    {
        extern ConfigManager configManager;
        return (status != PAIRING_IDLE) && (millis() - lastActivity) > configManager.getPairingBusyTimeout();
    }

    void updateActivity()
    {
        lastActivity = millis();
    }

    void reset()
    {
        status = PAIRING_IDLE;
        currentBike = "";
        requestTimeout = 0;
        updateActivity();
    }
};

static PairingState pairingState;

// Sistema de eventos
static BikeEventCallback eventCallback = nullptr;

// R19: callbacks BLE (task do NimBLE) apenas ENFILEIRAM eventos; todo o
// processamento — parsing, gestão de cadastro, persistência, pushes — roda
// no contexto do loop(). Fila limitada = backpressure explícito.
struct BleEvent {
    uint8_t type; // 0=connect 1=disconnect 2=data 3=config
    char bikeId[16];
    char payload[MAX_BLE_MESSAGE_SIZE + 1];
};
static QueueHandle_t eventQueue = nullptr;
static const uint8_t BLE_EVENT_CONNECT = 0;
static const uint8_t BLE_EVENT_DISCONNECT = 1;
static const uint8_t BLE_EVENT_DATA = 2;
static const uint8_t BLE_EVENT_CONFIG = 3;

static bool enqueueBleEvent(uint8_t type, const String& bikeId, const String& payload = "")
{
    if (!eventQueue) return false;
    if (payload.length() > MAX_BLE_MESSAGE_SIZE) return false;

    BleEvent ev;
    ev.type = type;
    strncpy(ev.bikeId, bikeId.c_str(), sizeof(ev.bikeId) - 1);
    ev.bikeId[sizeof(ev.bikeId) - 1] = '\0';
    strncpy(ev.payload, payload.c_str(), sizeof(ev.payload) - 1);
    ev.payload[sizeof(ev.payload) - 1] = '\0';

    if (xQueueSend(eventQueue, &ev, 0) != pdTRUE) {
        Serial.printf("⚠️ BLE event queue full - dropping %s event for %s\n",
                      type == BLE_EVENT_DATA ? "data" : "config", bikeId.c_str());
        return false;
    }
    return true;
}

// Sistema de fila com prioridade (consumida somente no loop — R19)
struct BikeQueueItem {
    String bikeId;
    String jsonData;
    uint8_t priority; // 1=alta (bateria baixa), 2=normal
    uint32_t timestamp;

    BikeQueueItem(const String& id, const String& data, uint8_t prio)
        : bikeId(id), jsonData(data), priority(prio), timestamp(millis()) {}
};

struct BikeQueueComparator {
    bool operator()(const BikeQueueItem& a, const BikeQueueItem& b) {
        if (a.priority != b.priority) {
            return a.priority > b.priority; // Menor número = maior prioridade
        }
        return a.timestamp > b.timestamp; // FIFO para mesma prioridade
    }
};

static std::priority_queue<BikeQueueItem, std::vector<BikeQueueItem>, BikeQueueComparator> priorityQueue;

// Callbacks BLE: fazem o mínimo e delegam ao loop via fila
static void onBikeConnected(const String &bikeId);
static void onBikeDisconnected(const String &bikeId);
static void onBikeDataReceived(const String &bikeId, const String &jsonData);
static void onConfigRequest(const String &bikeId, const String &request);

// Processadores reais (contexto do loop)
static void processBikeConnected(const String &bikeId);
static void processBikeDisconnected(const String &bikeId);
static void processBikeData(const String &bikeId, const String &jsonData);
static void processConfigRequest(const String &bikeId, const String &request);

void BikePairing::enter()
{
    Serial.println("🔵 Entering BIKE_PAIRING mode");

    if (!eventQueue) {
        eventQueue = xQueueCreate(16, sizeof(BleEvent));
    } else {
        xQueueReset(eventQueue);
    }

    // Initialize bike manager (carrega status + configs + merge do registry)
    BikeManager::init();

    pairingState.reset();

    // F12: start() é idempotente — não recria serviços
    if (!BPRBLEServer::start())
    {
        Serial.println("❌ Failed to start BLE Server");
        return;
    }

    // Register BLE callbacks
    BPRBLEServer::setDataCallback(onBikeDataReceived);
    BPRBLEServer::setConnectCallback(onBikeConnected);
    BPRBLEServer::setDisconnectCallback(onBikeDisconnected);
    BPRBLEServer::setConfigCallback(onConfigRequest);

    BPRBLEServer::setBusyStatus(false);
}

void BikePairing::update()
{
    uint32_t now = millis();
    static uint32_t lastAdvertisingUpdate = 0;

    if (now - lastAdvertisingUpdate > 5000) {
        BPRBLEServer::updateAdvertisingStatus();
        BPRBLEServer::checkConnectionTimeouts();
        lastAdvertisingUpdate = now;
    }

    // R19: drena os eventos BLE no contexto do loop
    if (eventQueue) {
        BleEvent ev;
        while (xQueueReceive(eventQueue, &ev, 0) == pdTRUE) {
            String bikeId(ev.bikeId);
            String payload(ev.payload);
            switch (ev.type) {
                case BLE_EVENT_CONNECT: processBikeConnected(bikeId); break;
                case BLE_EVENT_DISCONNECT: processBikeDisconnected(bikeId); break;
                case BLE_EVENT_DATA: processBikeData(bikeId, payload); break;
                case BLE_EVENT_CONFIG: processConfigRequest(bikeId, payload); break;
            }
        }
    }

    // F11: envia pushes enfileirados (ACKs/configs) no contexto do loop
    BPRBLEServer::processPushQueue();

    // Processar fila de dados sequencialmente
    processDataQueue();
}

void BikePairing::exit()
{
    while (!priorityQueue.empty())
    {
        priorityQueue.pop();
    }
    pairingState.reset();
    if (eventQueue) {
        xQueueReset(eventQueue);
    }

    // Marcar BLE como BUSY durante sync (bike respeita e mantém dados)
    BPRBLEServer::setBusyStatus(true, 120);

    Serial.println("🔚 Exiting BIKE_PAIRING mode - BLE marked as BUSY");
}

void BikePairing::printStatus()
{
    int bikes = BikePairing::getConnectedBikes();
    Serial.printf("🚲 Bikes conectadas: %d | 💾 Heap: %d bytes\n", bikes, ESP.getFreeHeap());

    uint32_t stateTime = millis() - stateStartTime;
    uint32_t syncInterval = configManager.getConfig().sync_interval_ms();
    uint32_t nextSync = (syncInterval - stateTime) / 1000;

    if (stateTime < syncInterval)
    {
        Serial.printf("🔄 Próxima sync em: %lus\n", nextSync);
        return;
    }
    Serial.println("🔄 Sync pendente...");
}

uint8_t BikePairing::getConnectedBikes()
{
    return BPRBLEServer::getConnectedBikes();
}

PairingStatus BikePairing::getStatus()
{
    if (pairingState.isTimedOut())
    {
        pairingState.status = PAIRING_IDLE;
    }
    return pairingState.status;
}

bool BikePairing::isSafeToExit()
{
    // Se buffer está cheio, forçar sync (prioridade máxima)
    if (bufferManager.isFull()) {
        Serial.println("🚨 Buffer full - forcing sync!");
        return true;
    }

    // Se está processando bike, não pode sair
    if (getStatus() != PAIRING_IDLE) {
        return false;
    }

    // Se há bikes com dados pendentes na fila, não pode sair
    if (!priorityQueue.empty()) {
        Serial.printf("📋 %d bikes in queue - not safe to exit\n", priorityQueue.size());
        return false;
    }

    uint8_t connectedBikes = BPRBLEServer::getConnectedBikes();
    if (connectedBikes > 0) {
        static uint32_t lastConnectedCheck = 0;
        uint32_t now = millis();

        if (now - lastConnectedCheck > 30000) {
            Serial.printf("🔍 %d bikes connected - waiting for data or timeout\n", connectedBikes);
            lastConnectedCheck = now;
        }

        uint32_t stateTime = now - stateStartTime;
        if (stateTime < 60000) {
            return false;
        }
    }

    return true;
}

// ---- Callbacks (task BLE): apenas enfileiram ----------------------------

static void onBikeConnected(const String &bikeId)
{
    enqueueBleEvent(BLE_EVENT_CONNECT, bikeId);
}

static void onBikeDisconnected(const String &bikeId)
{
    enqueueBleEvent(BLE_EVENT_DISCONNECT, bikeId);
}

static void onBikeDataReceived(const String &bikeId, const String &jsonData)
{
    enqueueBleEvent(BLE_EVENT_DATA, bikeId, jsonData);
}

static void onConfigRequest(const String &bikeId, const String &request)
{
    enqueueBleEvent(BLE_EVENT_CONFIG, bikeId, request);
}

// ---- Processadores (contexto do loop) ------------------------------------

static void processBikeConnected(const String &bikeId)
{
    BikePairing::triggerEvent(BIKE_ARRIVED, BPRBLEServer::getConnectedBikes());
    Serial.printf("🚲 Bike %s connected\n", bikeId.c_str());

    if (!BikeManager::canConnect(bikeId))
    {
        // F03: desconhecida vira pending no primeiro contato; blocked é desconectada
        if (!BikeManager::isKnownBike(bikeId)) {
            BikeManager::addPendingBike(bikeId);
            Serial.printf("📝 Bike %s registered as pending\n", bikeId.c_str());
        } else {
            Serial.printf("❌ Blocked bike %s - disconnecting\n", bikeId.c_str());
            BPRBLEServer::forceDisconnectBike(bikeId);
            return;
        }
    }

    // Se tem config pendente, enviar imediatamente
    if (BikeManager::hasConfigUpdate(bikeId))
    {
        pairingState.status = PAIRING_SENDING_CONFIG;
        pairingState.updateActivity();

        String config = BikeManager::getConfigForBike(bikeId);
        if (!config.isEmpty()) {
            BPRBLEServer::pushConfigToBike(bikeId, config);
            BikeManager::markConfigSent(bikeId);
            Serial.printf("⚙️ Config sent to %s on connection\n", bikeId.c_str());
        }
    }
}

static void processBikeDisconnected(const String &bikeId)
{
    BikePairing::triggerEvent(BIKE_LEFT, BPRBLEServer::getConnectedBikes());
    Serial.printf("🚲 Bike %s disconnected\n", bikeId.c_str());

    // X03: desconexão libera a vaga de processamento imediatamente — não
    // esperamos o timeout de 30 s para a próxima bike da fila
    if (pairingState.currentBike == bikeId)
    {
        BikePairing::finishCurrentBike();
    }

    // X03: itens enfileirados (não processados = não ACKados) da conexão
    // encerrada são descartados com reconciliação definida — a bike mantém
    // os dados até ACK final e reenvia na próxima conexão
    if (!priorityQueue.empty())
    {
        std::priority_queue<BikeQueueItem, std::vector<BikeQueueItem>, BikeQueueComparator> kept;
        size_t dropped = 0;
        while (!priorityQueue.empty())
        {
            BikeQueueItem item = priorityQueue.top();
            priorityQueue.pop();
            if (item.bikeId == bikeId)
            {
                dropped++;
            }
            else
            {
                kept.push(item);
            }
        }
        priorityQueue = kept;
        if (dropped > 0)
        {
            Serial.printf("📋 Dropped %d queued items from disconnected %s (bike will resend)\n",
                          (int)dropped, bikeId.c_str());
        }
    }
}

// ---- PRD lotes/recibos (BTP1) — processadores no contexto do loop --------

static void sendBatchStatusFor(const String &bikeId, const BatchSpoolHeader &h) {
    const char *state =
        h.state == BATCH_STATE_RECEIVING ? "receiving" :
        h.state == BATCH_STATE_COMPLETE_LOCAL ? "complete_local" :
        h.state == BATCH_STATE_AWAITING_CLOUD ? "awaiting_cloud" :
        h.state == BATCH_STATE_HASH_MISMATCH ? "hash_mismatch" : "receiving";

    uint8_t pages = (uint8_t)((h.total_parts + 63) / 64);
    for (uint8_t p = 0; p < pages; p++) {
        uint8_t bitmap8[8];
        if (BatchSpool::missingPage(bikeId, h.session_id, h.session_salt, p, pages, bitmap8)) {
            String msg = BPRJsonHelper::createBatchStatusPage(
                bikeId, h.session_id, h.session_salt, state, p, pages, bitmap8);
            BPRBLEServer::pushConfigToBike(bikeId, msg);
        }
    }
    Serial.printf("📤 batch_status %s enviado (%d páginas) para %s lote %lu-%d\n",
                  state, pages, bikeId.c_str(), (unsigned long)h.session_id, h.session_salt);
}

static void sendClearAuthFromReceipt(const String &bikeId, const BatchReceiptData &r) {
    char shaHex[65];
    batch_sha256_hex(r.sha256, shaHex);
    String msg = BPRJsonHelper::createBatchClearAuth(
        bikeId, r.session_id, r.session_salt, r.total_parts, r.total_bytes,
        shaHex, r.confirmed_at);
    BPRBLEServer::pushConfigToBike(bikeId, msg);
    Serial.printf("📤 Autorização repetida pelo recibo (lote %lu-%d) para %s\n",
                  (unsigned long)r.session_id, r.session_salt, bikeId.c_str());
}

static bool parseShaHex(const String &hex, uint8_t out[32]) {
    if (hex.length() != 64) return false;
    for (int i = 0; i < 32; i++) {
        char byte[3] = {hex[i * 2], hex[i * 2 + 1], 0};
        char *end = nullptr;
        long v = strtol(byte, &end, 16);
        if (end == byte || v < 0 || v > 255) return false;
        out[i] = (uint8_t)v;
    }
    return true;
}

static void processBatchAnnounce(const String &bikeId, const String &jsonData);

// ---- Patch de horários: referência de hora e eventos de viagem ----------

static uint32_t g_timeRefSeq = 0;

// §5 do patch: a central responde CADA heartbeat com a hora CORRENTE da sua
// sincronização NTP (não um número congelado), validade e idade do sync
static void sendTimeRef(const String &bikeId) {
    time_t now = time(nullptr);
    bool valid = TimeSync::isTimeValid();
    uint32_t refId = ++g_timeRefSeq;
    if (refId == 0) refId = 1;

    DynamicJsonDocument doc(320);
    doc["type"] = "time_ref";
    doc["v"] = 1;
    doc["bike_id"] = bikeId;
    doc["ref_id"] = refId;
    doc["utc_s"] = valid ? (uint32_t)now : 0; // SEGUNDOS UTC (patch)
    doc["valid"] = valid;
    doc["base_id"] = configCredentials.getBaseId();
    // Qualidade: idade aproximada da última sincronização NTP
    doc["sync_age_sec"] = valid ? (uint32_t)(now - TimeSync::lastNtpSyncTime()) : 0;

    String msg;
    serializeJson(doc, msg);
    BPRBLEServer::pushConfigToBike(bikeId, msg);
}

// Eventos/metadados de viagem (imutáveis por event_id): a central acumula
// em /trips/{bike}_{seq}.json e o sync publica na nuvem
static bool saveTripRecord(const String &bikeId, const String &jsonData,
                           bool isMeta) {
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, jsonData) != DeserializationError::Ok) return false;

    uint32_t tripSeq = doc["trip_seq"] | 0;
    if (tripSeq == 0) return false;
    String bikeIdField = doc["bike_id"] | "";
    if (bikeIdField != bikeId) return false; // identidade da conexão

    if (!LittleFS.exists("/trips")) {
        LittleFS.mkdir("/trips");
    }
    String path = "/trips/" + bikeId + "_" + String(tripSeq) + ".json";

    // Lê o existente e faz MERGE (eventos por event_id nunca sobrescritos)
    DynamicJsonDocument merged(2048);
    File existing = LittleFS.open(path, "r");
    if (existing) {
        deserializeJson(merged, existing);
        existing.close();
    }
    merged["v"] = 1;
    merged["bike_id"] = bikeId;
    merged["trip_seq"] = tripSeq;
    merged["updated_at"] = millis();

    if (isMeta) {
        // sessões e referências: união sem duplicar
        JsonArray sessions = merged.createNestedArray("sessions");
        for (JsonObject s : doc["sessions"].as<JsonArray>()) {
            bool dup = false;
            for (JsonObject m : merged["sessions"].as<JsonArray>()) {
                if ((uint32_t)m["session_id"] == (uint32_t)s["session_id"] &&
                    (uint16_t)m["session_salt"] == (uint16_t)s["session_salt"]) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                JsonObject o = sessions.createNestedObject();
                o["session_id"] = s["session_id"];
                o["session_salt"] = s["session_salt"];
            }
        }
        JsonArray refs = merged.createNestedArray("time_refs");
        for (JsonObject r : doc["time_refs"].as<JsonArray>()) {
            bool dup = false;
            for (JsonObject m : merged["time_refs"].as<JsonArray>()) {
                if ((uint32_t)m["ref_id"] == (uint32_t)r["ref_id"] &&
                    (uint16_t)m["boot_id"] == (uint16_t)r["boot_id"]) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                JsonObject o = refs.createNestedObject();
                o["ref_id"] = r["ref_id"];
                o["civil_s"] = r["civil_s"];
                o["civil_valid"] = r["civil_valid"];
                o["counter_ms"] = r["counter_ms"];
                o["boot_id"] = r["boot_id"];
            }
        }
        if (doc.containsKey("planned_sleep_sec")) {
            merged["planned_sleep_sec"] = doc["planned_sleep_sec"];
            merged["sleep_nature"] = doc["sleep_nature"] | "requested";
        }
    } else {
        JsonArray events = merged.createNestedArray("events");
        uint32_t eventId = doc["event_id"] | 0;
        for (JsonObject m : merged["events"].as<JsonArray>()) {
            if ((uint32_t)m["event_id"] == eventId) {
                // Evento confirmado é IMUTÁVEL no reenvio (§4)
                return true;
            }
        }
        JsonObject o = events.createNestedObject();
        o["event_id"] = eventId;
        o["kind"] = doc["kind"] | "";
        o["base_id"] = doc["base_id"] | "";
        o["civil_s"] = doc["civil_s"] | 0;
        o["civil_valid"] = doc["civil_valid"] | false;
        o["counter_ms"] = doc["counter_ms"] | 0;
        o["boot_id"] = doc["boot_id"] | 0;
        o["method"] = doc["method"] | "";
    }

    // Gravação recuperável (tmp + rename)
    String tmp = path + ".tmp";
    File out = LittleFS.open(tmp, "w");
    if (!out) return false;
    serializeJson(merged, out);
    out.close();
    LittleFS.remove(path);
    if (!LittleFS.rename(tmp, path)) {
        LittleFS.remove(tmp);
        return false;
    }
    return true;
}

static void processBatchAnnounce(const String &bikeId, const String &jsonData) {
    DynamicJsonDocument doc(BLE_COMMAND_BUFFER);
    if (deserializeJson(doc, jsonData) != DeserializationError::Ok) return;

    uint8_t v = doc["v"] | 0;
    if (v != BATCH_PROTO_VERSION) {
        // T20: versão não negociada — recusa com diagnóstico, dados preservados
        Serial.printf("🚨 Announce v%d não suportado (bike %s) - busy\n", v, bikeId.c_str());
        String busy = BPRJsonHelper::createBusyResponse(bikeId, 60);
        BPRBLEServer::pushConfigToBike(bikeId, busy);
        return;
    }

    uint32_t sid = doc["session_id"] | 0;
    uint16_t salt = doc["session_salt"] | 0;
    uint16_t total = doc["total"] | 0;
    uint32_t totalBytes = doc["total_bytes"] | 0;
    String shaHex = doc["sha256"] | "";
    uint8_t sha[32];
    if (sid == 0 || salt == 0 || !parseShaHex(shaHex, sha)) {
        Serial.printf("🚨 Announce inválido de %s - ignorado\n", bikeId.c_str());
        return;
    }

    // Recibo existente: a autorização é repetida pelo recibo, SEM payload
    // (T11: "pode apagar" perdido → central responde de novo; T18: outra
    // estação recupera a conclusão canônica pela identidade)
    BatchReceiptData receipt;
    if (BatchSpool::hasReceipt(bikeId, sid, salt, receipt)) {
        sendClearAuthFromReceipt(bikeId, receipt);
        return;
    }

    // Lote em andamento: retomada (mesma identidade+conteúdo) ou conflito
    BatchSpoolHeader existing;
    if (BatchSpool::find(bikeId, sid, salt, existing)) {
        if (existing.total_parts == total && existing.total_bytes == totalBytes &&
            memcmp(existing.sha256, sha, 32) == 0) {
            sendBatchStatusFor(bikeId, existing); // pendências ATUAIS (T07/T08)
        } else {
            // Mesma identidade, conteúdo diferente (PRD 4.1) — conflito
            // explícito; bike preserva e não recebe autorização
            Serial.printf("🚨 Conflito de identidade: lote %lu-%d de %s com conteúdo diferente\n",
                          (unsigned long)sid, salt, bikeId.c_str());
            const uint8_t zeros[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            String msg = BPRJsonHelper::createBatchStatusPage(
                bikeId, sid, salt, "conflict", 0, 1, zeros);
            BPRBLEServer::pushConfigToBike(bikeId, msg);
        }
        return;
    }

    // Admissão com reserva de capacidade (T15) — sem espaço: busy explícito
    String reason;
    if (!BatchSpool::admit(bikeId, sid, salt, total, totalBytes, sha, reason)) {
        if (reason == "receipt_exists") return; // corrida com o bloco acima
        Serial.printf("⚠️ Lote %lu-%d de %s não admitido: %s\n",
                      (unsigned long)sid, salt, bikeId.c_str(), reason.c_str());
        String busy = BPRJsonHelper::createBusyResponse(bikeId, 60);
        BPRBLEServer::pushConfigToBike(bikeId, busy);
        return;
    }

    // Admitido: informa as pendências (todas as partes, ainda)
    BatchSpoolHeader h;
    if (BatchSpool::find(bikeId, sid, salt, h)) {
        sendBatchStatusFor(bikeId, h);
    }
}

static void processBatchPart(const String &bikeId, const String &jsonData,
                             uint32_t sid, uint16_t salt, uint16_t seq, bool isFinal) {
    // O spool do lote precisa estar admitido (announce) — sem lote, cai no
    // caminho legado (compat T20)
    BatchSpoolHeader h;
    if (!BatchSpool::find(bikeId, sid, salt, h)) return;

    // Bytes da parte = os bytes EXATOS recebidos no write GATT (contrato
    // BTP1: CRC/SHA sobre os bytes como transmitidos, sem re-serialização)
    int stored = BatchSpool::addPart(bikeId, sid, salt, seq,
                                     (const uint8_t *)jsonData.c_str(),
                                     (uint16_t)jsonData.length());
    if (stored < 0) {
        // T03: mesmo índice com conteúdo diferente — conflito explícito
        String ack = BPRJsonHelper::createDataAckConflict(bikeId, sid, salt, seq);
        BPRBLEServer::pushConfigToBike(bikeId, ack);
        return;
    }
    if (stored == 0) {
        // Persistência não confirmada (T06): a bike conserva e reenvia
        Serial.printf("⚠️ Parte %d de %s não persistiu - pedindo reenvio\n",
                      seq, bikeId.c_str());
        String busy = BPRJsonHelper::createBusyResponse(bikeId, 5);
        BPRBLEServer::pushConfigToBike(bikeId, busy);
        return;
    }

    // ACK da parte — SEM autorizar limpeza (PRD §3: a autorização vem só da
    // confirmação durável da nuvem; can_clear_buffer é sempre false aqui)
    String ack = BPRJsonHelper::createDataAckResponse(bikeId, sid, salt, seq,
                                                       isFinal, false);
    BPRBLEServer::pushConfigToBike(bikeId, ack);

    // Conjunto completo? Conferir hash global (uma vez; T05)
    BatchSpoolHeader now;
    if (BatchSpool::find(bikeId, sid, salt, now)) {
        uint16_t present = 0;
        for (uint16_t i = 0; i < now.total_parts; i++) {
            if (now.part_bitmap[i / 8] & (1u << (i % 8))) present++;
        }
        if (present == now.total_parts) {
            BatchSpool::checkComplete(bikeId, sid, salt);
        }
    }
}

static void processBikeData(const String &bikeId, const String &jsonData)
{
    // R19: admissão vinculada ao estado real da central, não só ao prazo de
    // busy — durante CLOUD_SYNC/CONFIG_AP a recepção é recusada
    if (currentState != STATE_BIKE_PAIRING)
    {
        Serial.printf("⚠️ Data rejected from %s - central not in PAIRING state\n", bikeId.c_str());
        String busyResponse = BPRJsonHelper::createBusyResponse(bikeId, 30);
        BPRBLEServer::pushConfigToBike(bikeId, busyResponse);
        return;
    }

    // Rejeitar dados se central está busy
    if (BPRBLEServer::isCentralBusy())
    {
        Serial.printf("⚠️ Data rejected from %s - Central is BUSY\n", bikeId.c_str());
        String busyResponse = BPRJsonHelper::createBusyResponse(bikeId, 30);
        BPRBLEServer::pushConfigToBike(bikeId, busyResponse);
        return;
    }

    if (!BikeManager::canConnect(bikeId))
    {
        Serial.printf("❌ Data rejected from non-allowed bike: %s\n", bikeId.c_str());
        BikeManager::recordPendingVisit(bikeId);
        return;
    }

    DynamicJsonDocument doc(BIKE_DATA_BUFFER);
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error)
    {
        Serial.printf("❌ JSON parse error: %s\n", error.f_str());
        return;
    }

    String type = doc["type"] | "data";

    // ---- Patch de horários: eventos/metadados de viagem -------------------
    if (type == "trip_event")
    {
        if (saveTripRecord(bikeId, jsonData, false)) {
            Serial.printf("🧭 Evento de viagem de %s registrado\n", bikeId.c_str());
        }
        return;
    }
    if (type == "trip_meta")
    {
        if (saveTripRecord(bikeId, jsonData, true)) {
            Serial.printf("🧭 Metadados de viagem de %s registrados\n", bikeId.c_str());
        }
        return;
    }

    // ---- PRD lotes/recibos (BTP1): roteamento do protocolo de lotes ------
    if (type == "batch_announce")
    {
        processBatchAnnounce(bikeId, jsonData);
        return;
    }

    if (type == "batch_cleared")
    {
        // Bike confirmou a limpeza (PRD 5.2) — diagnóstico do ciclo fechado
        uint32_t sid = doc["session_id"] | 0;
        uint16_t salt = doc["session_salt"] | 0;
        String sha = doc["sha256"] | "";
        Serial.printf("✅ Bike %s confirmou limpeza do lote %lu-%d (sha %s)\n",
                      bikeId.c_str(), (unsigned long)sid, salt, sha.c_str());
        return;
    }

    if (type == "heartbeat")
    {
        int batteryPercent = doc["battery_percent"] | 0;
        int heap = doc["heap"] | 0;
        uint32_t bikeConfigVersion = doc["config_version"] | 0;

        uint16_t sessions = 0;
        uint32_t bytes = 0;
        uint32_t oldestTs = 0;
        uint8_t bufferPercent = 0;

        if (doc.containsKey("pending_data"))
        {
            JsonObject pendingData = doc["pending_data"];
            sessions = pendingData["sessions_count"] | 0;
            bytes = pendingData["total_bytes"] | 0;
        }

        BikeManager::updateHeartbeat(bikeId, batteryPercent, heap, sessions, bytes, oldestTs, bufferPercent);

        // Patch de horários (§5): TODO heartbeat recebe a referência de hora
        // CORRENTE da central — a bike guarda a associação contador/civil
        sendTimeRef(bikeId);

        // Verificar se precisa atualizar config (versão por bike)
        if (BikeManager::needsConfigUpdate(bikeId, bikeConfigVersion))
        {
            pairingState.status = PAIRING_SENDING_CONFIG;
            pairingState.updateActivity();

            String config = BikeManager::getConfigForBike(bikeId);
            if (!config.isEmpty()) {
                BPRBLEServer::pushConfigToBike(bikeId, config);
                BikeManager::markConfigSent(bikeId);
                Serial.printf("⚙️ Auto-config sent to %s (bike_version=%lu)\n",
                              bikeId.c_str(), (unsigned long)bikeConfigVersion);
            }
        }
        return;
    }

    // type == "data": parte de lote (BTP1) ou fragmento legado
    {
        uint32_t sid = doc["session_id"] | 0;
        uint16_t salt = doc["session_salt"] | 0;
        uint16_t seq = doc["seq"] | 0;
        bool isFinal = doc["is_final"] | false;

        // Lote admitido (announce) → protocolo de lotes: spool próprio,
        // CRC por parte e SEM can_clear (a autorização vem da nuvem)
        BatchSpoolHeader lot;
        if (sid != 0 && salt != 0 && BatchSpool::find(bikeId, sid, salt, lot)) {
            if (pairingState.currentBike.isEmpty() ||
                pairingState.currentBike == bikeId) {
                processBatchPart(bikeId, jsonData, sid, salt, seq, isFinal);
                return;
            }
            uint8_t priority = BikeManager::isBatteryLow(bikeId) ? 1 : 2;
            BikePairing::enqueueBike(bikeId, jsonData, priority);
            return;
        }
        // Sem lote admitido: caminho legado (firmware antigo — T20)
    }

    if (pairingState.currentBike.isEmpty() || pairingState.currentBike == bikeId)
    {
        BikePairing::processDataFromBike(bikeId, jsonData);
    }
    else
    {
        uint8_t priority = BikeManager::isBatteryLow(bikeId) ? 1 : 2;
        BikePairing::enqueueBike(bikeId, jsonData, priority);
    }
}

static void processConfigRequest(const String &bikeId, const String &request)
{
    pairingState.status = PAIRING_SENDING_CONFIG;
    pairingState.updateActivity();

    // R18/X09: autorização verificada NO LOOP — não mais na task do NimBLE
    // (ConfigCallbacks), que concorria com o loop alterando bikeStatus
    if (!BikeManager::canConnect(bikeId))
    {
        Serial.printf("❌ Config request from non-allowed bike: %s - ignored\n", bikeId.c_str());
        pairingState.status = PAIRING_IDLE;
        return;
    }

    DynamicJsonDocument doc(BLE_COMMAND_BUFFER);
    DeserializationError error = deserializeJson(doc, request);

    if (error)
    {
        Serial.printf("❌ Config request parse error: %s\n", error.f_str());
        pairingState.status = PAIRING_IDLE;
        return;
    }

    String type = doc["type"] | "config_request";

    // F06/R08: confirmação de aplicação pela bike
    if (type == "config_ack")
    {
        uint32_t appliedVersion = doc["applied_version"] | 0;
        // R08: compara CONTEÚDO da string, não endereços de literais
        String status = doc["status"] | "error";
        bool ok = (status == "ok");
        BikeManager::markConfigAcked(bikeId, ok ? appliedVersion : 0);
        pairingState.status = PAIRING_IDLE;
        return;
    }

    if (type == "config_request")
    {
        String config = BikeManager::getConfigForBike(bikeId);
        if (!config.isEmpty())
        {
            BPRBLEServer::pushConfigToBike(bikeId, config);
            BikeManager::markConfigSent(bikeId);
        }
        pairingState.status = PAIRING_IDLE;
    }
}

// Implementação dos métodos de processamento
void BikePairing::processDataQueue()
{
    uint32_t now = millis();

    if (!pairingState.currentBike.isEmpty() && (now - pairingState.requestTimeout) > BIKE_DATA_TIMEOUT_MS)
    {
        Serial.printf("⏰ Bike %s timeout - finishing\n", pairingState.currentBike.c_str());
        finishCurrentBike();
    }

    if (pairingState.currentBike.isEmpty() && !priorityQueue.empty())
    {
        BikeQueueItem nextItem = priorityQueue.top();
        priorityQueue.pop();

        Serial.printf("🚀 Processing next bike: %s (priority: %d)\n",
                     nextItem.bikeId.c_str(), nextItem.priority);

        processDataFromBike(nextItem.bikeId, nextItem.jsonData);
    }
}

void BikePairing::processDataFromBike(const String &bikeId, const String &jsonData)
{
    pairingState.currentBike = bikeId;
    pairingState.status = PAIRING_RECEIVING_DATA;
    pairingState.updateActivity();
    pairingState.requestTimeout = millis();

    // F16: addBikeData só retorna true com persistência íntegra (gravação verificada)
    if (bufferManager.addBikeData(bikeId, jsonData))
    {
        Serial.printf("💾 Fragment persisted from %s\n", bikeId.c_str());

        // F05: ACK vinculado a sessão/fragmento persistidos
        DynamicJsonDocument doc(BIKE_DATA_BUFFER);
        if (deserializeJson(doc, jsonData) == DeserializationError::Ok)
        {
            uint32_t sessionId = doc["session_id"] | 0;
            uint16_t salt = doc["session_salt"] | 0;
            uint16_t seq = doc["seq"] | 0;
            uint16_t total = doc["total"] | 0;
            bool isFinal = doc["is_final"] | false;

            bool canClear = isFinal && total > 0 &&
                            bufferManager.hasCompleteSession(bikeId, sessionId, salt, total);

            String ack = BPRJsonHelper::createDataAckResponse(
                bikeId, sessionId, salt, seq, isFinal, canClear);
            BPRBLEServer::pushConfigToBike(bikeId, ack);

            // X03: sessão completa confirmável = sucesso — libera a vaga para
            // a próxima bike em vez de ocupá-la até o timeout de 30 s
            if (isFinal && canClear)
            {
                finishCurrentBike();
            }
        }
    }
    else
    {
        // Persistência falhou ou buffer cheio: bike mantém os dados e tenta depois
        Serial.printf("⚠️ Cannot store fragment from %s - asking to retry\n", bikeId.c_str());
        String busyResponse = BPRJsonHelper::createBusyResponse(bikeId, 30);
        BPRBLEServer::pushConfigToBike(bikeId, busyResponse);
    }
}

void BikePairing::enqueueBike(const String &bikeId, const String &jsonData, uint8_t priority)
{
    // Seção 6: capacidade DEFINIDA — a fila não cresce sem limite na heap;
    // item recusado recebe busy (a bike mantém os dados até ACK e reenvia)
    if (priorityQueue.size() >= MAX_BLE_QUEUE_ITEMS)
    {
        Serial.printf("⚠️ BLE data queue full (%d) - refusing %s (busy)\n",
                      MAX_BLE_QUEUE_ITEMS, bikeId.c_str());
        String busyResponse = BPRJsonHelper::createBusyResponse(bikeId, 30);
        BPRBLEServer::pushConfigToBike(bikeId, busyResponse);
        return;
    }

    priorityQueue.emplace(bikeId, jsonData, priority);

    const char* priorityStr = (priority == 1) ? "HIGH (battery low)" : "NORMAL";
    Serial.printf("📋 Bike %s enqueued with %s priority (queue size: %d)\n",
                 bikeId.c_str(), priorityStr, priorityQueue.size());
}

void BikePairing::finishCurrentBike()
{
    if (!pairingState.currentBike.isEmpty())
    {
        Serial.printf("✅ Finished processing bike %s\n", pairingState.currentBike.c_str());
        pairingState.currentBike = "";
        pairingState.requestTimeout = 0;
        pairingState.status = PAIRING_IDLE;
    }
}

void BikePairing::setEventCallback(BikeEventCallback callback)
{
    eventCallback = callback;
}

void BikePairing::triggerEvent(BikeEvent event, uint8_t bikeCount)
{
    if (eventCallback)
    {
        eventCallback(event, bikeCount);
    }
}
