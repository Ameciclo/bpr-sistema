#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include "bike_manager.h"
#include "binary_structs.h"
#include "bpr_json_helper.h"
#include "buffer_manager.h"
#include "config_credentials.h"
#include "constants.h"
#include "time_sync.h"

extern BufferManager bufferManager;
extern ConfigCredentials configCredentials;

// Global data for bike management
static BikeStatusData bikeStatus;
static BikeConfigData bikeConfigs;
static std::map<String, bool> configChanged;
static std::map<String, uint32_t> configAckedVersion;
static bool dataLoaded = false;
static bool configsLoaded = false;

static int findBikeSlot(const BikeStatusData& data, const String& bikeId) {
    uint32_t id = bikeIdToInt(bikeId);
    if (id == 0) return -1;
    for (int i = 0; i < data.bike_count; i++) {
        if (data.bike_ids[i] == id) return i;
    }
    return -1;
}

static int findConfigSlot(const BikeConfigData& data, const String& bikeId) {
    uint32_t id = bikeIdToInt(bikeId);
    if (id == 0) return -1;
    for (int i = 0; i < data.bike_count; i++) {
        if (data.bike_ids[i] == id) return i; // F13: compara pelo ID canônico, não String(número)
    }
    return -1;
}

bool BikeManager::init()
{
    Serial.println("💾 BikeManager::init() - Loading bike data and configs");

    if (!loadData()) {
        Serial.println("⚠️ Failed to load bike data, using defaults");
    }

    if (!loadBikeConfigs()) {
        Serial.println("⚠️ Failed to load bike configs, using defaults");
    }

    // F03: cadastro baixado do Firebase é a fonte de verdade de autorização
    mergeFromRegistry();

    dataLoaded = true;
    return true;
}

bool BikeManager::isLoaded()
{
    return dataLoaded;
}

bool BikeManager::loadData()
{
    if (!LittleFS.exists(BIKE_STATUS_FILE))
    {
        Serial.println("📄 Bike data not found, creating empty");
        memset(&bikeStatus, 0, sizeof(bikeStatus));
        dataLoaded = true;
        return saveData();
    }

    File file = LittleFS.open(BIKE_STATUS_FILE, "r");
    if (!file)
    {
        Serial.println("❌ Failed to open bike data");
        return false;
    }

    size_t bytesRead = file.readBytes((char*)&bikeStatus, sizeof(BikeStatusData));
    file.close();

    if (bytesRead != sizeof(BikeStatusData) || bikeStatus.bike_count > 10)
    {
        Serial.printf("❌ Data size/count invalid: %d bytes, %d bikes\n", bytesRead, bikeStatus.bike_count);
        memset(&bikeStatus, 0, sizeof(bikeStatus));
        return false;
    }

    dataLoaded = true;
    Serial.printf("✅ Bike data loaded: %d bikes\n", bikeStatus.bike_count);

    int allowed = 0, pending = 0, blocked = 0, unknown = 0;
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        BikeStatus status = (BikeStatus)bikeStatus.statuses[i];
        switch (status) {
            case STATUS_ALLOWED: allowed++; break;
            case STATUS_PENDING: pending++; break;
            case STATUS_BLOCKED: blocked++; break;
            default: unknown++; break;
        }
    }
    Serial.printf("   Allowed: %d | Pending: %d | Blocked: %d | Unknown: %d\n", allowed, pending, blocked, unknown);

    return true;
}

bool BikeManager::saveData()
{
    File file = LittleFS.open(BIKE_STATUS_FILE, "w");
    if (!file)
    {
        Serial.println("❌ Failed to create bike data");
        return false;
    }

    size_t bytesWritten = file.write((uint8_t*)&bikeStatus, sizeof(BikeStatusData));
    file.close();

    if (bytesWritten != sizeof(BikeStatusData))
    {
        Serial.printf("❌ Failed to write bike data: %d != %d\n", bytesWritten, sizeof(BikeStatusData));
        return false;
    }

    return true;
}

// F03: converte o cadastro baixado (bike_registry.bin) na lista operacional,
// preservando telemetria local (bateria, últimos contatos).
// R15: política explícita de capacidade e revogação:
// - slots cheios de PENDING cedem lugar a um ALLOWED do cadastro remoto;
// - cadastro válido E não-vazio que omite uma bike ALLOWED local a rebaixa
//   para BLOCKED (revogação por ausência, auditada em log);
// - cadastro vazio/nulo é "sem decisão" (não revoga nada — provisionamento).
bool BikeManager::mergeFromRegistry()
{
    if (!LittleFS.exists(BIKE_REGISTRY_FILE))
    {
        return false;
    }

    File file = LittleFS.open(BIKE_REGISTRY_FILE, "r");
    if (!file) return false;

    BikeRegistryData registry;
    size_t bytesRead = file.readBytes((char*)&registry, sizeof(registry));
    file.close();

    if (bytesRead != sizeof(registry) || registry.bike_count > 10)
    {
        Serial.println("❌ Bike registry invalid - not merging");
        return false;
    }

    if (registry.bike_count == 0)
    {
        Serial.println("ℹ️ Bike registry empty - no revocation, no merge");
        return true;
    }

    bool changed = false;
    for (int r = 0; r < registry.bike_count; r++)
    {
        uint32_t id = registry.bikes[r];
        if (id == 0) continue;

        int slot = -1;
        for (int i = 0; i < bikeStatus.bike_count; i++)
        {
            if (bikeStatus.bike_ids[i] == id) { slot = i; break; }
        }

        if (slot == -1)
        {
            if (bikeStatus.bike_count >= 10)
            {
                // R15: pendentes locais cedem lugar a autorizações remotas
                int evict = -1;
                uint32_t oldestCreated = UINT32_MAX;
                for (int i = 0; i < bikeStatus.bike_count; i++)
                {
                    if ((BikeStatus)bikeStatus.statuses[i] == STATUS_PENDING &&
                        bikeStatus.created_at[i] < oldestCreated)
                    {
                        oldestCreated = bikeStatus.created_at[i];
                        evict = i;
                    }
                }
                if (evict != -1)
                {
                    uint32_t evictedId = bikeStatus.bike_ids[evict];
                    Serial.printf("🗑️ Pending bike %s evicted for allowed %s\n",
                                  intToBikeId(evictedId).c_str(), intToBikeId(id).c_str());
                    slot = evict; // reutiliza o slot do pendente
                }
                else
                {
                    // R15: sem espaço e sem pendente evictable — merge NÃO
                    // afirma sucesso sem aplicar a autorização
                    Serial.printf("❌ Registry merge: no slot for allowed bike %s\n",
                                  intToBikeId(id).c_str());
                    return false;
                }
            }
            else
            {
                slot = bikeStatus.bike_count;
                bikeStatus.bike_count++;
            }
            bikeStatus.bike_ids[slot] = id;
            bikeStatus.created_at[slot] = registry.created_at[r];
            bikeStatus.last_contacts[slot] = registry.last_seen[r];
            bikeStatus.battery_levels[slot] = 0;
        }
        else
        {
            if (bikeStatus.created_at[slot] == 0) {
                bikeStatus.created_at[slot] = registry.created_at[r];
            }
        }

        uint8_t newStatus = registry.statuses[r];
        if (bikeStatus.statuses[slot] != newStatus) {
            bikeStatus.statuses[slot] = newStatus;
        }
        changed = true;
    }

    // R15: revogação por ausência — ALLOWED local fora do snapshot vira BLOCKED
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if ((BikeStatus)bikeStatus.statuses[i] != STATUS_ALLOWED) continue;

        bool present = false;
        for (int r = 0; r < registry.bike_count; r++)
        {
            if (registry.bikes[r] == bikeStatus.bike_ids[i]) { present = true; break; }
        }
        if (!present)
        {
            Serial.printf("🚫 Bike %s revoked (absent from registry snapshot)\n",
                          intToBikeId(bikeStatus.bike_ids[i]).c_str());
            bikeStatus.statuses[i] = STATUS_BLOCKED;
            changed = true;
        }
    }

    bikeStatus.last_update = registry.last_update;

    if (changed) {
        saveData();
        Serial.printf("✅ Registry merged: %d bikes (%d in operational list)\n",
                      registry.bike_count, bikeStatus.bike_count);
    }
    return true;
}

bool BikeManager::canConnect(const String &bikeId)
{
    if (!dataLoaded)
        return false;

    if (!isValidBikeIdFormat(bikeId))
    {
        Serial.printf("❌ Invalid bike ID format: %s\n", bikeId.c_str());
        return false;
    }

    int slot = findBikeSlot(bikeStatus, bikeId);
    if (slot == -1)
    {
        // F03: desconhecida entra no fluxo pending, não é autorizada a enviar
        Serial.printf("❌ Bike %s not in registry - connection rejected\n", bikeId.c_str());
        return false;
    }

    BikeStatus status = (BikeStatus)bikeStatus.statuses[slot];
    bool allowed = (status == STATUS_ALLOWED);

    Serial.printf("🔍 Bike %s in registry: %s\n",
                  bikeId.c_str(), allowed ? "✅ Can connect" : "❌ Not allowed");
    return allowed;
}

bool BikeManager::isAllowed(const String &bikeId)
{
    if (!dataLoaded)
        return false;

    if (!isValidBikeIdFormat(bikeId))
        return false;

    int slot = findBikeSlot(bikeStatus, bikeId);
    if (slot == -1)
        return false;

    return (BikeStatus)bikeStatus.statuses[slot] == STATUS_ALLOWED;
}

bool BikeManager::isKnownBike(const String &bikeId)
{
    if (!dataLoaded)
        return false;
    return findBikeSlot(bikeStatus, bikeId) != -1;
}

void BikeManager::addPendingBike(const String &bikeId)
{
    if (!isValidBikeIdFormat(bikeId)) {
        Serial.printf("❌ Cannot add pending bike with invalid id: %s\n", bikeId.c_str());
        return;
    }
    if (findBikeSlot(bikeStatus, bikeId) != -1) {
        return; // já conhecida
    }
    if (bikeStatus.bike_count >= 10) {
        Serial.println("❌ Max bikes reached, cannot add new bike");
        return;
    }

    time_t now = time(nullptr);
    if (now < 1000000000L) now = 0; // F20: sem horário válido, registra 0

    int index = bikeStatus.bike_count;
    bikeStatus.bike_ids[index] = bikeIdToInt(bikeId);
    bikeStatus.statuses[index] = STATUS_PENDING;
    bikeStatus.created_at[index] = now;
    bikeStatus.last_contacts[index] = now;
    bikeStatus.battery_levels[index] = 0;
    bikeStatus.bike_count++;

    saveData();
    Serial.printf("📝 Bike %s added as pending\n", bikeId.c_str());
}

void BikeManager::updateHeartbeat(const String &bikeId, int battery, int heap,
                                   uint16_t sessions, uint32_t bytes, uint32_t oldestTs, uint8_t bufferPercent)
{
    if (!dataLoaded)
        return;

    int bikeIndex = findBikeSlot(bikeStatus, bikeId);
    if (bikeIndex == -1)
        return;

    time_t now = time(nullptr);
    if (now < 1000000000L) now = 0;

    bikeStatus.last_contacts[bikeIndex] = now;
    bikeStatus.battery_levels[bikeIndex] = battery;

    // §6.6: persiste a cada 12 heartbeats para não perder contatos em reboot
    static int persistCounter = 0;
    if (++persistCounter >= 12) {
        persistCounter = 0;
        saveData();
    }

    Serial.printf("💓 Heartbeat updated: %s (bat:%d%%, heap:%d, pending:%d sessions/%d bytes)\n",
                  bikeId.c_str(), battery, heap, sessions, bytes);
}

// R15: publica TODAS as descobertas pendentes (a política de "menos de 5
// minutos" descartava justamente as descobertas mais antigas)
bool BikeManager::getPendingBikesForUpload(DynamicJsonDocument &doc)
{
    if (!dataLoaded)
        return false;

    time_t now = time(nullptr);
    if (now < 1000000000L) return false; // sem relógio não há last_contact útil

    doc.clear();

    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        BikeStatus status = (BikeStatus)bikeStatus.statuses[i];
        if (status == STATUS_PENDING && bikeStatus.created_at[i] > 0)
        {
            String bikeId = intToBikeId(bikeStatus.bike_ids[i]);
            JsonObject bikeObj = doc.createNestedObject(bikeId);
            bikeObj["status"] = status;
            bikeObj["first_seen"] = bikeStatus.created_at[i];
            bikeObj["last_contact"] = bikeStatus.last_contacts[i];
        }
    }

    return doc.size() > 0;
}

int BikeManager::getAllowedCount()
{
    if (!dataLoaded)
        return 0;

    int count = 0;
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if ((BikeStatus)bikeStatus.statuses[i] == STATUS_ALLOWED)
            count++;
    }
    return count;
}

void BikeManager::recordPendingVisit(const String &bikeId)
{
    if (!dataLoaded)
        return;

    int bikeIndex = findBikeSlot(bikeStatus, bikeId);
    if (bikeIndex == -1 || (BikeStatus)bikeStatus.statuses[bikeIndex] != STATUS_PENDING)
        return;

    time_t now = time(nullptr);
    if (now < 1000000000L) now = 0;
    bikeStatus.last_contacts[bikeIndex] = now;

    saveData();
    Serial.printf("📝 Pending bike %s visited\n", bikeId.c_str());
}

int BikeManager::getPendingCount()
{
    if (!dataLoaded)
        return 0;

    int count = 0;
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if ((BikeStatus)bikeStatus.statuses[i] == STATUS_PENDING)
            count++;
    }
    return count;
}

void BikeManager::logConfigEvent(const String &bikeId, const String &event, bool success)
{
    Serial.printf("📝 Config event: %s - %s (%s)\n",
                  bikeId.c_str(), event.c_str(), success ? "SUCCESS" : "FAILED");
}

int BikeManager::getConnectedCount()
{
    if (!dataLoaded)
        return 0;

    time_t now = time(nullptr);
    if (now < 1000000000L) return 0; // F20: sem relógio válido não há como medir recência

    int count = 0;
    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if ((now - bikeStatus.last_contacts[i]) < 120)
        {
            count++;
        }
    }
    return count;
}

void BikeManager::populateHeartbeatData(JsonArray &bikes_array)
{
    if (!dataLoaded)
        return;

    time_t now = time(nullptr);

    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        String bikeId = intToBikeId(bikeStatus.bike_ids[i]);
        JsonObject bikeData = bikes_array.createNestedObject();

        bikeData["id"] = bikeId;
        bikeData["status"] = bikeStatus.statuses[i];
        bikeData["last_seen"] = bikeStatus.last_contacts[i];
        bikeData["battery_last"] = bikeStatus.battery_levels[i];
        bikeData["first_seen"] = bikeStatus.created_at[i];

        uint32_t timeSince = (now > bikeStatus.last_contacts[i]) ? (now - bikeStatus.last_contacts[i]) : 0;
        bikeData["seconds_since_contact"] = timeSince;
        bikeData["is_recent"] = (timeSince < 300);
    }
}

bool BikeManager::hasConfigUpdate(const String &bikeId)
{
    return configChanged.find(bikeId) != configChanged.end() && configChanged[bikeId];
}

void BikeManager::markConfigSent(const String &bikeId)
{
    // F06: enviado ≠ confirmado; pendência só sai com ACK da bike
    configChanged[bikeId] = true;
    Serial.printf("📤 Config sent to %s - waiting for ACK\n", bikeId.c_str());
}

void BikeManager::markConfigAcked(const String &bikeId, uint32_t appliedVersion)
{
    // R08: pendência só é encerrada quando o ACK confirma a revisão esperada;
    // versão zero/antiga/diferente mantém a pendência com erro visível
    int i = findConfigSlot(bikeConfigs, bikeId);
    uint32_t expected = (i != -1) ? bikeConfigs.config_versions[i] : 0;

    if (appliedVersion == 0 || (expected != 0 && appliedVersion != expected))
    {
        Serial.printf("⚠️ Config ACK from %s ignored (applied %lu, expected %lu) - still pending\n",
                      bikeId.c_str(), (unsigned long)appliedVersion, (unsigned long)expected);
        return;
    }

    configChanged[bikeId] = false;
    configAckedVersion[bikeId] = appliedVersion;
    Serial.printf("✅ Config ACK from %s (applied version %lu)\n", bikeId.c_str(), (unsigned long)appliedVersion);
}

uint32_t BikeManager::getAckedConfigVersion(const String &bikeId)
{
    auto it = configAckedVersion.find(bikeId);
    return (it != configAckedVersion.end()) ? it->second : 0;
}

String BikeManager::getConfigForBike(const String &bikeId)
{
    if (!configsLoaded) {
        Serial.printf("⚠️ Configs not loaded for %s\n", bikeId.c_str());
        return "";
    }

    int i = findConfigSlot(bikeConfigs, bikeId);
    if (i == -1) {
        Serial.printf("⚠️ No config found for %s\n", bikeId.c_str());
        return "";
    }

    // F06: envelope versionado com destinatário e versão por bike
    DynamicJsonDocument doc(512);
    doc["type"] = "config_push";
    doc["bike_id"] = bikeId;
    doc["config_version"] = bikeConfigs.config_versions[i];

    JsonObject cfg = doc.createNestedObject("config");
    cfg["wifi"]["scan_interval_sec"] = bikeConfigs.scan_intervals[i];
    cfg["wifi"]["scan_timeout_ms"] = bikeConfigs.scan_timeouts[i];
    cfg["wifi"]["max_networks"] = bikeConfigs.max_networks[i];
    cfg["wifi"]["rssi_threshold"] = bikeConfigs.rssi_thresholds[i];
    cfg["power"]["deep_sleep_duration_sec"] = bikeConfigs.sleep_durations[i];
    cfg["ble"]["scan_time_sec"] = bikeConfigs.ble_scan_times[i];
    cfg["dev_mode"] = (bikeConfigs.dev_modes[i] == 1);

    String result;
    serializeJson(doc, result);
    return result;
}

String BikeManager::generateSystemHeartbeat()
{
    if (!dataLoaded)
        return "";

    DynamicJsonDocument heartbeat(CENTRAL_HEARTBEAT_BUFFER);

    heartbeat["timestamp"] = time(nullptr);
    heartbeat["uptime_sec"] = millis() / 1000;
    heartbeat["heap_free"] = ESP.getFreeHeap();

    JsonArray bikes_array = heartbeat.createNestedArray("bikes");
    populateHeartbeatData(bikes_array);

    heartbeat["total_bikes"] = bikes_array.size();
    heartbeat["bikes_allowed"] = getAllowedCount();
    heartbeat["bikes_pending"] = getPendingCount();
    heartbeat["bikes_with_recent_contact"] = getConnectedCount();

    String result;
    serializeJson(heartbeat, result);
    return result;
}

bool BikeManager::needsConfigUpdate(const String &bikeId, uint32_t bikeLastUpdate)
{
    if (!configsLoaded) return false;

    int i = findConfigSlot(bikeConfigs, bikeId);
    if (i == -1) return false;

    // F13/F20: versão por bike, não um timestamp global
    return bikeConfigs.config_versions[i] > bikeLastUpdate;
}

std::vector<String> BikeManager::getBikesWithUpdates()
{
    std::vector<String> bikes;
    for (auto& pair : configChanged) {
        if (pair.second) {
            bikes.push_back(pair.first);
        }
    }
    return bikes;
}

// Getters simplificados para BikePairing (O06: valores reais do BufferManager)
uint32_t BikeManager::getPendingBytes(const String &bikeId)
{
    uint16_t sessions = 0;
    uint32_t bytes = 0;
    bufferManager.getBikePendingInfo(bikeId, sessions, bytes);
    return bytes;
}

uint16_t BikeManager::getPendingSessions(const String &bikeId)
{
    uint16_t sessions = 0;
    uint32_t bytes = 0;
    bufferManager.getBikePendingInfo(bikeId, sessions, bytes);
    return sessions;
}

uint8_t BikeManager::getBufferUsage(const String &bikeId)
{
    uint16_t sessions = 0;
    uint32_t bytes = 0;
    bufferManager.getBikePendingInfo(bikeId, sessions, bytes);
    return (uint8_t)min<uint32_t>((bytes * 100UL) / (MAX_ITEMS_PER_BIKE * MAX_ITEM_DATA_SIZE), 100);
}

bool BikeManager::isBatteryLow(const String &bikeId)
{
    if (!dataLoaded)
        return false;

    int i = findBikeSlot(bikeStatus, bikeId);
    if (i == -1) return false;

    return bikeStatus.battery_levels[i] <= 25;
}

bool BikeManager::loadBikeConfigs()
{
    if (!LittleFS.exists(BIKE_CONFIGS_FILE))
    {
        Serial.println("📄 Bike configs not found, creating defaults");
        memset(&bikeConfigs, 0, sizeof(bikeConfigs));
        bikeConfigs.last_update = 0;
        bikeConfigs.bike_count = 0;
        configsLoaded = true;
        return saveBikeConfigs();
    }

    File file = LittleFS.open(BIKE_CONFIGS_FILE, "r");
    if (!file)
    {
        Serial.println("❌ Failed to open bike configs");
        return false;
    }

    size_t bytesRead = file.readBytes((char*)&bikeConfigs, sizeof(BikeConfigData));
    file.close();

    if (bytesRead != sizeof(BikeConfigData) || bikeConfigs.bike_count > 10)
    {
        Serial.printf("❌ Config size/count invalid: %d bytes, %d bikes\n", bytesRead, bikeConfigs.bike_count);
        memset(&bikeConfigs, 0, sizeof(bikeConfigs));
        return false;
    }

    configsLoaded = true;
    Serial.printf("✅ Bike configs loaded: %d bikes\n", bikeConfigs.bike_count);
    return true;
}

bool BikeManager::saveBikeConfigs()
{
    File file = LittleFS.open(BIKE_CONFIGS_FILE, "w");
    if (!file)
    {
        Serial.println("❌ Failed to create bike configs");
        return false;
    }

    size_t bytesWritten = file.write((uint8_t*)&bikeConfigs, sizeof(BikeConfigData));
    file.close();

    if (bytesWritten != sizeof(BikeConfigData))
    {
        Serial.printf("❌ Failed to write bike configs: %d != %d\n", bytesWritten, sizeof(BikeConfigData));
        return false;
    }

    return true;
}

// F13: lista real de bikes autorizadas (não IDs de exemplo)
std::vector<String> BikeManager::getAllowedBikeIds()
{
    std::vector<String> ids;
    if (!dataLoaded) return ids;

    for (int i = 0; i < bikeStatus.bike_count; i++)
    {
        if ((BikeStatus)bikeStatus.statuses[i] == STATUS_ALLOWED)
        {
            ids.push_back(intToBikeId(bikeStatus.bike_ids[i]));
        }
    }
    return ids;
}

bool BikeManager::downloadSingleBikeConfig(const String& bikeId)
{
    HTTPClient http;

    int slot = findConfigSlot(bikeConfigs, bikeId);
    uint32_t localVersion = (slot != -1) ? bikeConfigs.config_versions[slot] : 0;

    // 1. Verificar versão remota desta bike.
    // R20: corpo "null" (chave inexistente) NÃO é versão zero — ausência é
    // distinguida de valor válido
    String timestampUrl = "/bases/" + configCredentials.getBaseId() + "/bike_configs/last_update/" + bikeId + ".json?auth=" + configCredentials.getFirebaseKey();
    String fullTimestampUrl = configCredentials.getFirebaseURL() + timestampUrl;

    http.begin(fullTimestampUrl);
    int timestampCode = http.GET();

    if (timestampCode == HTTP_CODE_OK) {
        String body = http.getString();
        http.end();
        body.trim();

        bool versionPresent = body.length() > 0 && !body.equalsIgnoreCase("null");
        if (versionPresent)
        {
            uint32_t remoteVersion = (uint32_t)body.toInt();
            if (remoteVersion == 0 || remoteVersion <= localVersion) {
                Serial.printf("⏭️ Config %s already up to date (local %lu, remote %lu)\n",
                              bikeId.c_str(), (unsigned long)localVersion, (unsigned long)remoteVersion);
                return true;
            }

            // 2. Baixar configuração
            return fetchAndApplyBikeConfig(bikeId, remoteVersion);
        }
        // versão ausente: sem config publicada para esta bike — não é erro
        Serial.printf("ℹ️ No published config version for %s - skipping\n", bikeId.c_str());
        return true;
    } else {
        http.end();
        Serial.printf("⚠️ No version for %s, downloading anyway\n", bikeId.c_str());
    }

    // Versão indisponível (erro de rede): baixa por segurança, mas a versão
    // aplicada será a publicada no corpo, se houver
    return fetchAndApplyBikeConfig(bikeId, 0);
}

bool BikeManager::fetchAndApplyBikeConfig(const String& bikeId, uint32_t remoteVersion)
{
    HTTPClient http;

    String configUrl = "/bases/" + configCredentials.getBaseId() + "/bike_configs/" + bikeId + ".json?auth=" + configCredentials.getFirebaseKey();
    String fullConfigUrl = configCredentials.getFirebaseURL() + configUrl;

    http.begin(fullConfigUrl);
    int configCode = http.GET();

    if (configCode != HTTP_CODE_OK) {
        Serial.printf("❌ Failed to download config for %s: HTTP %d\n", bikeId.c_str(), configCode);
        http.end();
        return false;
    }

    String arrayData = http.getString();
    http.end();

    // R20: corpo "null" = config inexistente para a bike — não aplica nada
    arrayData.trim();
    if (arrayData.length() == 0 || arrayData.equalsIgnoreCase("null")) {
        Serial.printf("ℹ️ No config published for %s - nothing to apply\n", bikeId.c_str());
        return true;
    }

    // 3. Converter array JSON para CSV: "[300,5000,20,-85,3600,5,0]"
    arrayData.replace("[", "");
    arrayData.replace("]", "");

    // 4. Aplicar configuração (R07: versão remota atravessa sem virar zero)
    bool success = updateBikeConfigFromCSV(bikeId, arrayData, remoteVersion);

    if (success) {
        Serial.printf("✅ Config downloaded for %s: %s (version %lu)\n",
                      bikeId.c_str(), arrayData.c_str(), (unsigned long)remoteVersion);
    }

    return success;
}

bool BikeManager::updateBikeConfigFromCSV(const String& bikeId, const String& csvData, uint32_t remoteVersion)
{
    if (!configsLoaded) {
        Serial.println("⚠️ Configs not loaded, cannot update");
        return false;
    }

    // F13: slot pelo ID canônico (String(numérico) nunca dava match)
    int bikeIndex = findConfigSlot(bikeConfigs, bikeId);

    if (bikeIndex == -1 && bikeConfigs.bike_count < 10) {
        bikeIndex = bikeConfigs.bike_count;
        bikeConfigs.bike_ids[bikeIndex] = bikeIdToInt(bikeId);
        bikeConfigs.bike_count++;
    }

    if (bikeIndex == -1) {
        Serial.printf("❌ No space for bike %s\n", bikeId.c_str());
        return false;
    }

    BikeConfigData before = bikeConfigs;

    // R20: valida em CANDIDATA antes de tocar a configuração ativa
    BikeConfigData candidate = bikeConfigs;
    int index = 0;
    int startPos = 0;
    int fields = 0;

    while (startPos < (int)csvData.length() && index < 7) {
        int commaPos = csvData.indexOf(',', startPos);
        String value = (commaPos == -1) ? csvData.substring(startPos) : csvData.substring(startPos, commaPos);

        switch (index) {
            case 0: candidate.scan_intervals[bikeIndex] = value.toInt(); break;
            case 1: candidate.scan_timeouts[bikeIndex] = value.toInt(); break;
            case 2: candidate.max_networks[bikeIndex] = value.toInt(); break;
            case 3: candidate.rssi_thresholds[bikeIndex] = value.toInt(); break;
            case 4: candidate.sleep_durations[bikeIndex] = value.toInt(); break;
            case 5: candidate.ble_scan_times[bikeIndex] = value.toInt(); break;
            case 6: candidate.dev_modes[bikeIndex] = value.toInt(); break;
        }

        fields++;
        if (commaPos == -1) break;
        startPos = commaPos + 1;
        index++;
    }

    // R20: CSV incompleto não é aplicado
    if (fields < 7) {
        Serial.printf("❌ Config CSV for %s incomplete (%d/7 fields) - rejected\n", bikeId.c_str(), fields);
        return false;
    }

    // R20: faixas validadas antes da conversão ter efeito
    if (candidate.scan_intervals[bikeIndex] < 5 || candidate.scan_intervals[bikeIndex] > 86400 ||
        candidate.scan_timeouts[bikeIndex] < 100 ||
        candidate.max_networks[bikeIndex] < 1 || candidate.max_networks[bikeIndex] > 20 ||
        candidate.sleep_durations[bikeIndex] < 60 ||
        candidate.ble_scan_times[bikeIndex] < 1) {
        Serial.printf("❌ Config CSV for %s out of range - rejected\n", bikeId.c_str());
        return false;
    }

    bikeConfigs = candidate;

    // F20: versão remota por bike, não hora local.
    // R07: versão zero NÃO sobrescreve a versão local conhecida
    if (remoteVersion > 0) {
        bikeConfigs.config_versions[bikeIndex] = remoteVersion;
        bikeConfigs.last_update = remoteVersion;
    }

    bool valuesChanged = (memcmp(&before, &bikeConfigs, sizeof(BikeConfigData)) != 0);
    if (valuesChanged) {
        // F13: só há pendência real quando os valores mudaram
        configChanged[bikeId] = true;
    }

    if (!saveBikeConfigs()) {
        // R20: falha de persistência restaura o estado anterior
        bikeConfigs = before;
        Serial.printf("❌ Config for %s not persisted - previous values kept\n", bikeId.c_str());
        return false;
    }

    Serial.printf("✅ Config updated for %s from CSV (version %lu)\n",
                  bikeId.c_str(), (unsigned long)bikeConfigs.config_versions[bikeIndex]);
    return true;
}
