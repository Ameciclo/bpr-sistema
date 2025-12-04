# BPR Bike Firmware v2.0

Sistema de firmware para bicicletas compartilhadas do ecossistema Bota Pra Rodar (BPR).

## 🎯 Características

- **Ultra baixo consumo** - Deep sleep e light sleep otimizados
- **Comunicação BLE** - Cliente para conectar com a Base
- **WiFi Scanning** - Coleta de dados de localização
- **Máquina de Estados** - 5 estados bem definidos
- **Armazenamento persistente** - Sistema LittleFS para até 20.000 registros WiFi
- **Monitoramento de bateria** - ADC calibrado com média móvel

## 🔧 Hardware

- **MCU**: Seeed Studio XIAO ESP32-C3 (4MB flash interno)
- **Bateria**: Leitura via ADC no pino A0
- **LED**: Pino 8 (indicador de status)
- **Botão**: Pino 9 (modo emergência)
- **Storage**: 1MB LittleFS (~20.000 registros WiFi)

## 📊 Estados de Operação

```
BOOT → Inicialização e detecção da base
  ├── Base encontrada → AT_BASE
  └── Base não encontrada → SCANNING

AT_BASE → Conectado via BLE na base
  ├── Sincroniza configurações
  ├── Envia dados coletados
  ├── Light sleep periódico
  └── Detecta saída → SCANNING

SCANNING → Coletando dados WiFi na rua
  ├── Scan periódico (5min padrão)
  ├── Procura base a cada ciclo
  ├── Bateria baixa → LOW_POWER
  └── Muito tempo fora → LOW_POWER

LOW_POWER → Modo economia de energia
  ├── Scans menos frequentes (15min)
  ├── Light sleep longo
  ├── Bateria crítica → DEEP_SLEEP
  └── Base encontrada → AT_BASE

DEEP_SLEEP → Hibernação profunda
  └── Wake-up por timer → BOOT
```

## 🔋 Otimizações de Energia

### Modo Base (AT_BASE)
- CPU: 80MHz
- WiFi: OFF
- BLE: Ativo com parâmetros otimizados
- Sleep: Light sleep 1min entre checks

### Modo Viagem (SCANNING)
- CPU: 160MHz (para WiFi)
- WiFi: Scan only, TX power reduzida
- BLE: Scan passivo para base
- Sleep: Light sleep entre scans

### Modo Economia (LOW_POWER)
- CPU: 80MHz
- Intervalos ampliados
- Sleep: Light sleep longo

### Hibernação (DEEP_SLEEP)
- Tudo desligado
- Wake-up: Timer RTC ou botão
- Consumo: <10µA

## 📡 Protocolo BLE

### Service UUID: `BAAD`

### Características:
- **F00D** (Config): Base → Bike (configurações)
- **BEEF** (Status): Bike → Base (status da bike)
- **CAFE** (Data): Bike → Base (dados WiFi)

### Estruturas de Dados:

```cpp
// Configurações (Base → Bike)
struct BikeConfig {
  uint8_t version;
  uint16_t scan_interval_sec;
  uint16_t scan_interval_low_batt_sec;
  uint16_t deep_sleep_sec;
  float min_battery_voltage;
  char base_ble_name[32];
  uint32_t timestamp;
};

// Status (Bike → Base)
struct BikeStatus {
  char bike_id[16];
  float battery_voltage;
  uint32_t last_scan_timestamp;
  uint8_t flags; // bit 0: low_battery
  uint16_t records_count;
};

// Dados WiFi (Bike → Base)
struct WifiRecord {
  uint32_t timestamp;
  uint8_t bssid[6];
  int8_t rssi;
  uint8_t channel;
};
```

## 🚀 Build e Upload

```bash
# Instalar PlatformIO
pip install platformio

# Build
cd firmware/bike
pio run

# Upload
pio run --target upload

# Monitor serial
pio device monitor
```

## 🔧 Configuração

### Configurações Padrão:
- **Scan interval**: 300s (5min)
- **Scan low battery**: 900s (15min)  
- **Deep sleep**: 3600s (1h)
- **Min battery**: 3.45V
- **Base name**: "BPR Base Station"

### Modo Emergência:
- Pressionar botão BOOT durante operação
- Opções: 'r' (restart) ou 'c' (continuar)

## 📈 Monitoramento

O sistema imprime status a cada 30 segundos:

```
==================================================
🚲 bike_001 | Estado: SCANNING | Uptime: 1234s
🔋 3.82V (85%) ✅ | 📡 42 registros
🔵 BLE: Desconectado | ⏱️ Último scan: 120s atrás
==================================================
```

## 🐛 Debug

- **Serial**: 115200 baud
- **Logs**: Detalhados por módulo
- **LED**: Indica estado atual
- **Botão**: Modo emergência

## 💾 Sistema de Armazenamento

### Capacidade:
- **Flash interno**: 4MB total
- **LittleFS**: ~1MB disponível
- **Registros WiFi**: ~20.000 (50 bytes cada)
- **Autonomia**: ~14 dias de coleta contínua

### Funcionamento:
```
Scan WiFi → Buffer RAM (50 registros)
    ↓ (buffer cheio)
Flush → /wifi_X.json (1000 registros/arquivo)
    ↓ (na base)
Export → JSON completo via BLE
    ↓ (upload OK)
Limpeza → Remove todos os arquivos
```

### Estrutura de Arquivos:
```
/wifi_index.txt     # Índice do arquivo atual
/wifi_0.json        # Primeiros 1000 registros
/wifi_1.json        # Próximos 1000 registros
/wifi_N.json        # Até esgotar espaço
/config.json        # Configurações da bike
```

## 📝 TODO

- [x] ~~Implementar persistência LittleFS completa~~
- [ ] Adicionar compressão binária (13 bytes vs 50 bytes JSON)
- [ ] Otimizar consumo BLE
- [ ] Implementar watchdog
- [ ] Adicionar OTA updates
- [ ] Wear leveling para flash