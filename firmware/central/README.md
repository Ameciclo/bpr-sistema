# Central Base Station Firmware

Firmware para o módulo Central da Base do sistema Bota Pra Rodar (BPR), desenvolvido para o Seeed Studio XIAO ESP32-C3.

## 🎯 Funcionalidades

### Comunicação e Sincronização
- ✅ Conexão WiFi automática com reconexão
- ✅ Sincronização NTP para timestamp preciso
- ✅ Sincronização bidirecional com Firebase Realtime Database
- ✅ Heartbeat periódico para monitoramento

### Bluetooth Low Energy (BLE)
- ✅ Servidor BLE com múltiplos serviços GATT
- ✅ Suporte a até 10 conexões simultâneas
- ✅ Troca de configurações e status com bicicletas
- ✅ Detecção automática de chegada/saída de bicicletas

### Resiliência e Buffer
- ✅ Buffer local para operação offline
- ✅ Sincronização automática quando conexão retorna
- ✅ Watchdog de sistema para estabilidade
- ✅ Auto-diagnóstico e monitoramento de saúde

## 🏗️ Arquitetura

O firmware utiliza FreeRTOS com 6 tarefas independentes:

1. **WiFiManager** - Gerencia conexão WiFi e sincronização NTP
2. **FirebaseSync** - Sincroniza dados com Firebase
3. **BLEServer** - Gerencia comunicação BLE com bicicletas
4. **EventHandler** - Processa eventos do sistema
5. **BufferManager** - Gerencia buffer offline
6. **SelfCheck** - Monitora saúde do sistema

## 📡 Serviços BLE

### Config Service (12345678-1234-1234-1234-123456789abc)
- **CONFIG_PACKET** - Pacote completo de configuração
- **BATTERY_THRESHOLD** - Limite de bateria baixa
- **SLEEP_INTERVAL** - Intervalo de deep sleep
- **WIFI_SCAN_INTERVAL** - Intervalo de scan WiFi

### Status Service (12345678-1234-1234-1234-123456789ac1)
- **BIKE_ID** - Identificador da bicicleta
- **BATTERY_LEVEL** - Nível atual da bateria
- **LAST_WIFI_SCAN** - Timestamp do último scan WiFi
- **MODE** - Modo de operação atual

### Time Service (12345678-1234-1234-1234-123456789ac6)
- **EPOCH_TS** - Timestamp Unix atual

## 🔧 Configuração

1. Siga as instruções em `setup.md` para configurar credenciais
2. Ajuste constantes em `include/config.h` se necessário
3. Compile e faça upload com PlatformIO

## 📊 Estruturas de Dados

### BPRConfigPacket (Base → Bicicleta)
```cpp
struct BPRConfigPacket {
    uint8_t version;
    uint16_t deepSleepSec;
    uint16_t wifiScanInterval;
    uint16_t wifiScanLowBatt;
    float minBatteryVoltage;
    uint32_t timestamp;
};
```

### BPRBikeStatus (Bicicleta → Base)
```cpp
struct BPRBikeStatus {
    char bikeId[8];
    float batteryVoltage;
    uint32_t lastWifiScan;
    uint8_t flags;
};
```

## 🚨 Alertas Gerados

- `arrived_base` - Bicicleta chegou à base
- `left_base` - Bicicleta saiu da base
- `battery_low` - Bateria baixa detectada
- `sync_failure` - Falha na sincronização

## 🔍 Monitoramento

O sistema monitora continuamente:
- Uso de memória heap
- Temperatura interna do ESP32
- Stack usage das tarefas
- Status das conexões WiFi e BLE
- Latência de sincronização Firebase

## 🛠️ Build e Deploy

```bash
# Instalar dependências
pio lib install

# Compilar
pio run

# Upload
pio run --target upload

# Monitor serial
pio device monitor
```

## 📋 Logs

O sistema gera logs detalhados via Serial (115200 baud) incluindo:
- Status de conexões
- Eventos de bicicletas
- Sincronização Firebase
- Métricas de sistema
- Alertas e erros