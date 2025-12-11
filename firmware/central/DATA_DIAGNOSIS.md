# BPR Central System - Diagnóstico de Dados

## 🚨 **Problemas Críticos Identificados**

### ❌ **1. MÚLTIPLAS FONTES DE CONFIGURAÇÃO CONFLITANTES**

#### **config_manager.cpp vs central_config.cpp**
- **config_manager.cpp**: Usa `CentralConfigCache` struct
- **central_config.cpp**: Usa `CentralConfig` struct  
- **CONFLITO**: Duas estruturas diferentes para a mesma coisa!

```cpp
// config_manager.h - CentralConfigCache (17 campos)
struct CentralConfigCache {
    char base_id[32];
    int sync_interval_sec;
    int wifi_timeout_sec;
    int led_pin;
    int firebase_batch_size;
    char central_name[32];
    int central_max_bikes;
    char wifi_ssid[32];
    char wifi_password[64];
    float central_lat;
    float central_lng;
    int led_boot_ms;
    int led_ble_ready_ms;
    int led_wifi_sync_ms;
    unsigned long configTimestamp;
    unsigned long lastUpdate;
    bool valid;
};

// central_config.h - CentralConfig (20+ campos)
struct CentralConfig {
    String base_id = "base01";                    // ← HARDCODED!
    String central_name = "BPR Base Station";     // ← HARDCODED!
    int sync_interval_sec = 300;                  // ← HARDCODED!
    int wifi_timeout_sec = 30;                    // ← HARDCODED!
    int cleanup_interval_sec = 60;                // ← HARDCODED!
    int led_count_interval_sec = 30;              // ← HARDCODED!
    int log_interval_sec = 15;                    // ← HARDCODED!
    int firebase_batch_size = 8000;               // ← HARDCODED!
    int https_timeout_ms = 10000;                 // ← HARDCODED!
    int firebase_response_timeout_ms = 5000;      // ← HARDCODED!
    int led_pin = 8;                              // ← HARDCODED!
    String ntp_server = "pool.ntp.org";           // ← HARDCODED!
    int timezone_offset = -10800;                 // ← HARDCODED!
    int ntp_update_interval_ms = 60000;           // ← HARDCODED!
    int firebase_port = 443;                      // ← HARDCODED!
    unsigned long min_valid_timestamp = 1600000000; // ← HARDCODED!
    struct {                                       // ← HARDCODED!
        int boot_ms = 100;
        int ble_ready_ms = 2000;
        int bike_arrived_ms = 150;
        int bike_left_ms = 800;
        int wifi_sync_ms = 500;
        int count_ms = 300;
        int count_pause_ms = 1500;
        int error_ms = 50;
    } led;
};
```

### ❌ **2. DADOS HARDCODED ESPALHADOS**

#### **central_config.h - Valores Padrão Hardcoded**
```cpp
String base_id = "base01";                    // ← HARDCODED
String central_name = "BPR Base Station";     // ← HARDCODED  
int sync_interval_sec = 300;                  // ← HARDCODED
int wifi_timeout_sec = 30;                    // ← HARDCODED
int led_pin = 8;                              // ← HARDCODED
String ntp_server = "pool.ntp.org";           // ← HARDCODED
int timezone_offset = -10800;                 // ← HARDCODED (GMT-3)
int firebase_batch_size = 8000;               // ← HARDCODED
```

#### **ble_simple.cpp - UUIDs e Nomes Hardcoded**
```cpp
static String deviceName = "BPR Base Station";  // ← HARDCODED
static String serviceUUID = "BAAD";             // ← HARDCODED
static String bikeIdUUID = "F00D";              // ← HARDCODED
static String batteryUUID = "BEEF";             // ← HARDCODED
```

#### **led_controller.cpp - Pino Hardcoded**
```cpp
void initLED() {
    pinMode(config.led_pin, OUTPUT);  // ← Depende de config global
}
```

#### **firebase_manager.cpp - Configurações Hardcoded**
```cpp
int firebase_port = 443;                        // ← HARDCODED
unsigned long min_valid_timestamp = 1600000000; // ← HARDCODED
```

### ❌ **3. LEITURA DE MÚLTIPLAS FONTES**

#### **Arquivo `/config.json` - Lido por 3 módulos diferentes:**
1. **config_manager.cpp**: `loadConfigCache()` 
2. **central_config.cpp**: `loadCentralConfig()`
3. **firebase_manager.cpp**: `uploadToFirebase()`, `downloadFromFirebase()`

#### **Firebase - Acessado por 2 módulos:**
1. **config_manager.cpp**: `downloadConfigs()` 
2. **central_config.cpp**: `downloadCentralConfig()`

#### **SPIFFS vs LittleFS - Inconsistência:**
- **ble_simple.cpp**: Usa `SPIFFS.begin()`
- **config_manager.cpp**: Usa `LittleFS.open()`
- **central_config.cpp**: Usa `LittleFS.open()`

### ❌ **4. VARIÁVEIS GLOBAIS CONFLITANTES**

#### **Múltiplas Instâncias de Config:**
```cpp
// config_manager.cpp
static CentralConfigCache configCache = {0};

// central_config.cpp  
CentralConfig config;  // ← GLOBAL EXTERN

// ble_simple.cpp
extern CentralMode currentMode;     // ← GLOBAL EXTERN
extern String pendingData;          // ← GLOBAL EXTERN
```

## 📊 **Mapeamento Completo de Fontes de Dados**

### **📁 Arquivos de Configuração**
| Arquivo | Lido Por | Formato | Conteúdo |
|---------|----------|---------|----------|
| `/config.json` | config_manager, central_config, firebase_manager | JSON | WiFi, Firebase, base_id |
| `/config_cache.json` | config_manager | JSON | Cache completo de configurações |
| `/ble_config.json` | ble_simple | JSON | UUIDs BLE, device name |

### **🔥 Firebase Paths**
| Path | Acessado Por | Operação | Dados |
|------|--------------|----------|-------|
| `/central_configs/{base_id}` | config_manager, central_config | GET | Configurações completas |
| `/central_configs/{base_id}/last_modified` | config_manager | GET | Timestamp de modificação |
| `/bases/{base_id}/last_heartbeat` | firebase_manager | PUT | Heartbeat da central |
| `/central_data/{base_id}/{timestamp}` | firebase_manager | PUT | Dados das bikes |

### **💾 Variáveis Globais**
| Variável | Definida Em | Usada Em | Tipo |
|----------|-------------|----------|------|
| `config` | central_config.cpp | led_controller, firebase_manager | CentralConfig |
| `configCache` | config_manager.cpp | config_manager | CentralConfigCache |
| `currentMode` | ??? | ble_simple | CentralMode |
| `pendingData` | ??? | ble_simple, firebase_manager | String |

### **🔧 Constantes Hardcoded por Arquivo**

#### **central_config.h**
- ✅ **Valores padrão**: 15+ constantes hardcoded
- ❌ **Problema**: Deveria vir de config centralizado

#### **ble_simple.cpp**  
- ✅ **UUIDs BLE**: 4 UUIDs hardcoded
- ❌ **Problema**: Deveria vir de arquivo de config

#### **firebase_manager.cpp**
- ✅ **Timeouts**: 3 timeouts hardcoded  
- ❌ **Problema**: Deveria ser configurável

#### **led_controller.cpp**
- ✅ **Timings LED**: Usa `config.led.*` (correto)
- ✅ **Pino LED**: Usa `config.led_pin` (correto)

#### **wifi_manager.cpp**
- ✅ **Timeouts**: Usa constantes de `config.h` (correto)
- ❌ **Problema**: Arquivo `config.h` não existe!

## 🔧 **Soluções Recomendadas**

### **1. Unificar Configurações**
```cpp
// Criar APENAS config_manager.cpp/.h
struct CentralConfig {
    // Configurações básicas
    char base_id[32] = "base01";
    char central_name[64] = "BPR Base Station";
    
    // Rede
    char wifi_ssid[32] = "";
    char wifi_password[64] = "";
    int wifi_timeout_sec = 30;
    
    // Firebase  
    int sync_interval_sec = 300;
    int firebase_batch_size = 8000;
    int firebase_port = 443;
    
    // BLE
    char ble_device_name[32] = "BPR Base Station";
    char ble_service_uuid[8] = "BAAD";
    char ble_bike_uuid[8] = "F00D";
    char ble_battery_uuid[8] = "BEEF";
    
    // Hardware
    int led_pin = 8;
    
    // Timestamps
    unsigned long config_timestamp = 0;
    unsigned long last_update = 0;
    bool valid = false;
};
```

### **2. Fonte Única de Verdade**
```cpp
// config_manager.cpp - ÚNICA fonte de configuração
extern CentralConfig g_config;

// Todos os outros arquivos:
#include "config_manager.h"
extern CentralConfig g_config;
```

### **3. Eliminar Arquivos Redundantes**
- ❌ **Remover**: `central_config.cpp/.h`
- ❌ **Remover**: `config_loader.cpp/.h` 
- ✅ **Manter**: `config_manager.cpp/.h` (unificado)

### **4. Padronizar Sistema de Arquivos**
```cpp
// Usar APENAS LittleFS em todos os arquivos
#include <LittleFS.h>

// Nunca mais usar SPIFFS
```

### **5. Centralizar Constantes**
```cpp
// config_manager.h - Todas as constantes
#define WIFI_TIMEOUT_MS 30000
#define FIREBASE_TIMEOUT_MS 10000
#define BLE_SCAN_TIME_SEC 3
#define LED_PIN_DEFAULT 8
#define NTP_SYNC_INTERVAL_MS 60000
```

## 🎯 **Plano de Refatoração**

### **Fase 1: Unificação (2-3 dias)**
1. Criar `config_manager.cpp/.h` unificado
2. Migrar todas as configurações para uma struct
3. Remover `central_config.cpp/.h`
4. Remover `config_loader.cpp/.h`

### **Fase 2: Padronização (1-2 dias)**  
1. Substituir SPIFFS por LittleFS em todos os arquivos
2. Centralizar todas as constantes hardcoded
3. Criar arquivo `constants.h` com defines

### **Fase 3: Limpeza (1 dia)**
1. Remover variáveis globais duplicadas
2. Padronizar includes
3. Validar que todos os módulos usam a mesma fonte

## ⚠️ **Riscos Atuais**

### **🔥 Crítico**
- **Configurações conflitantes**: Sistema pode usar valores diferentes
- **Memory leaks**: Múltiplas instâncias de configuração
- **Race conditions**: Múltiplos módulos alterando mesmos dados

### **🟡 Médio**  
- **Manutenção complexa**: Mudanças precisam ser feitas em 3+ lugares
- **Debug difícil**: Não fica claro qual valor está sendo usado
- **Inconsistência**: SPIFFS vs LittleFS

### **🟢 Baixo**
- **Performance**: Múltiplas leituras do mesmo arquivo
- **Código duplicado**: Lógica de parsing repetida

## 📋 **Análise Completa dos Arquivos Restantes**

### ❌ **5. MAIS HARDCODING E CONFLITOS IDENTIFICADOS**

#### **bike_manager.cpp - Constantes Hardcoded**
```cpp
static const int MAX_BIKES = 10;              // ← HARDCODED
if (now - it->lastSeen > 300) {               // ← HARDCODED (5 min timeout)
packet.deepSleepSec = 300;                    // ← HARDCODED
packet.wifiScanInterval = 25;                 // ← HARDCODED
packet.wifiScanLowBatt = 60;                  // ← HARDCODED
packet.minBatteryVoltage = 3.45;              // ← HARDCODED
```

#### **ntp_manager.cpp - Dependência de Config Global**
```cpp
// Usa config global sem verificar se existe
timeClient = new NTPClient(ntpUDP, config.ntp_server.c_str(), 
                          config.timezone_offset, 
                          config.ntp_update_interval_ms);

if (bikeTimestamp > config.min_valid_timestamp) {  // ← Depende de config
```

#### **state_machine.cpp - Variáveis Globais Expostas**
```cpp
// Variáveis globais definidas aqui mas usadas em outros arquivos
CentralMode currentMode = MODE_BLE_ONLY;      // ← GLOBAL
String pendingData = "";                      // ← GLOBAL
unsigned long lastSync = 0;                   // ← GLOBAL
unsigned long modeStart = 0;                  // ← GLOBAL
```

#### **bike_discovery.cpp - Firebase URL Hardcoded**
```cpp
String configData = "{\"firebase\":{\"database_url\":\"https://botaprarodar-routes-default-rtdb.firebaseio.com\"},";
// ↑ URL do Firebase HARDCODED!
```

#### **setup_server.cpp - Múltiplas Constantes Hardcoded**
```cpp
#define SETUP_SERVER_PORT 80                  // ← HARDCODED
#define SETUP_AP_PASSWORD "botaprarodar"      // ← HARDCODED
#define SETUP_AP_PREFIX "BPR_Setup_"          // ← HARDCODED
#define SETUP_SERVER_IP "192.168.4.1"        // ← HARDCODED
#define FIREBASE_DEFAULT_URL "https://botaprarodar-routes-default-rtdb.firebaseio.com"  // ← HARDCODED
#define CENTRAL_NAME_PREFIX "BPR_"            // ← HARDCODED
```

#### **self_check.cpp - Referências a Arquivos Inexistentes**
```cpp
#include "config.h"  // ← ARQUIVO NÃO EXISTE!

// Usa constantes indefinidas:
SELFCHECK_INTERVAL_MS  // ← NÃO DEFINIDA

// Referencia TaskHandles que não existem:
extern TaskHandle_t wifiTaskHandle;     // ← NÃO DEFINIDO
extern TaskHandle_t firebaseTaskHandle; // ← NÃO DEFINIDO
```

### ❌ **6. INCONSISTÊNCIAS DE DADOS ADICIONAIS**

#### **Múltiplas Definições de Firebase URL:**
- **bike_discovery.cpp**: `"https://botaprarodar-routes-default-rtdb.firebaseio.com"`
- **setup_server.cpp**: `"https://botaprarodar-routes-default-rtdb.firebaseio.com"`
- **config.json**: URL configurável
- **PROBLEMA**: 3 fontes diferentes para mesma informação!

#### **Timeouts Inconsistentes:**
- **bike_manager.cpp**: 300s (5 min) para cleanup
- **central_config.h**: 30s para wifi_timeout
- **firebase_manager.cpp**: 10000ms para https_timeout
- **PROBLEMA**: Cada arquivo define seus próprios timeouts!

#### **Prefixos BLE Conflitantes:**
- **ble_simple.cpp**: Procura por "BPR_*"
- **setup_server.cpp**: Cria AP "BPR_Setup_*"
- **central_config.h**: Nome padrão "BPR Base Station"
- **PROBLEMA**: Múltiplas definições do prefixo BPR!

### 📊 **Mapeamento Completo ATUALIZADO**

#### **🔧 Constantes Hardcoded por Arquivo (COMPLETO)**

| Arquivo | Constantes Hardcoded | Problema |
|---------|---------------------|----------|
| **central_config.h** | 15+ valores padrão | Deveria ser configurável |
| **ble_simple.cpp** | 4 UUIDs BLE | Deveria vir de config |
| **firebase_manager.cpp** | 3 timeouts | Deveria ser configurável |
| **bike_manager.cpp** | 6 constantes (MAX_BIKES, timeouts, voltagens) | Deveria vir de config |
| **setup_server.cpp** | 7 defines (portas, IPs, URLs) | Deveria ser configurável |
| **bike_discovery.cpp** | 1 Firebase URL | Deveria vir de config |
| **ntp_manager.cpp** | 0 (usa config global) | ✅ Correto |
| **state_machine.cpp** | 0 (usa config global) | ✅ Correto |
| **self_check.cpp** | Referências quebradas | ❌ Arquivo config.h não existe |

#### **💾 Variáveis Globais COMPLETAS**

| Variável | Definida Em | Usada Em | Tipo | Status |
|----------|-------------|----------|------|--------|
| `config` | central_config.cpp | 8+ arquivos | CentralConfig | ❌ Conflito |
| `configCache` | config_manager.cpp | config_manager | CentralConfigCache | ❌ Conflito |
| `currentMode` | state_machine.cpp | ble_simple, state_machine | CentralMode | ❌ Global exposta |
| `pendingData` | state_machine.cpp | ble_simple, firebase_manager, bike_discovery | String | ❌ Global exposta |
| `lastSync` | state_machine.cpp | state_machine | unsigned long | ✅ Local |
| `modeStart` | state_machine.cpp | ble_simple, state_machine | unsigned long | ❌ Global exposta |
| `connectedBikes` | bike_manager.cpp | bike_manager | std::vector | ✅ Local |
| `pendingBikes` | bike_discovery.cpp | bike_discovery | std::vector | ✅ Local |

#### **📁 Arquivos de Sistema COMPLETOS**

| Arquivo | Lido Por | Formato | Status |
|---------|----------|---------|--------|
| `/config.json` | config_manager, central_config, firebase_manager, state_machine | JSON | ❌ Múltiplas fontes |
| `/config_cache.json` | config_manager | JSON | ✅ Fonte única |
| `/ble_config.json` | ble_simple | JSON | ✅ Fonte única |
| `config.h` | self_check | Header | ❌ NÃO EXISTE |

### 🔧 **Soluções ATUALIZADAS**

#### **6. Criar Arquivo de Constantes Centralizadas**
```cpp
// constants.h - TODAS as constantes do sistema
#define MAX_BIKES_DEFAULT 10
#define BIKE_TIMEOUT_SEC 300
#define WIFI_TIMEOUT_SEC 30
#define FIREBASE_TIMEOUT_MS 10000
#define SETUP_SERVER_PORT 80
#define SETUP_AP_PASSWORD "botaprarodar"
#define FIREBASE_DEFAULT_URL "https://botaprarodar-routes-default-rtdb.firebaseio.com"
#define BPR_PREFIX "BPR_"
#define BLE_SERVICE_UUID "BAAD"
#define BLE_BIKE_UUID "F00D"
#define BLE_BATTERY_UUID "BEEF"
```

#### **7. Eliminar Variáveis Globais Expostas**
```cpp
// state_machine.h - Interface limpa
class StateMachine {
private:
    CentralMode currentMode;
    String pendingData;
    unsigned long modeStart;
public:
    void handleCurrentMode();
    CentralMode getCurrentMode();
    void addPendingData(String data);
};
```

#### **8. Corrigir Referências Quebradas**
```cpp
// Remover #include "config.h" de self_check.cpp
// Criar constants.h com todas as definições
// Definir TaskHandles corretamente ou remover referências
```

### 🎯 **Plano de Refatoração ATUALIZADO**

#### **Fase 1: Emergencial (1 dia)**
1. Criar `constants.h` com TODAS as constantes
2. Corrigir `self_check.cpp` (remover referências quebradas)
3. Eliminar duplicação de Firebase URL

#### **Fase 2: Unificação (2-3 dias)**
1. Unificar `config_manager.cpp/.h` (eliminar central_config)
2. Encapsular variáveis globais em classes
3. Padronizar LittleFS em todos os arquivos

#### **Fase 3: Limpeza (1-2 dias)**
1. Remover arquivos redundantes
2. Validar todas as referências
3. Testes de integração

### ⚠️ **Riscos ATUALIZADOS**

#### **🔥 Crítico (NOVOS)**
- **Referências quebradas**: `self_check.cpp` não compila
- **URLs duplicadas**: Firebase URL em 3 lugares diferentes
- **Variáveis globais expostas**: Race conditions garantidas
- **TaskHandles inexistentes**: Crash em runtime

#### **🟡 Médio (CONFIRMADOS)**
- **15+ arquivos** com hardcoding
- **4 estruturas diferentes** para configuração
- **3 sistemas de arquivos** (SPIFFS/LittleFS/inexistente)

---

**Status**: 🚨 **CRÍTICO EXTREMO** - Sistema não compila e tem comportamento imprevisível
**Impacto**: Múltiplas falhas de compilação + race conditions + configurações conflitantes
**Ação**: Refatoração URGENTE obrigatória - sistema atual é inviável