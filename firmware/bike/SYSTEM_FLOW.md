# BPR Bike System - Fluxo de Funcionamento

## 🎯 Visão Geral

Sistema de bicicleta compartilhada com comunicação BLE e coleta de dados WiFi para geolocalização offline.

## 🗂️ Arquitetura de Arquivos

### 📁 Estrutura Modular
```
firmware/bike/src/
├── main.cpp              # 🚀 Loop principal e máquina de estados
├── wifi_scanner.cpp/.h   # 📡 Scanner WiFi com cache local
├── ble_client.cpp/.h     # 🔵 Cliente BLE para comunicação
├── battery_monitor.cpp/.h # 🔋 Monitor de bateria e alertas
├── power_manager.cpp/.h  # ⚡ Gerenciamento de energia/sleep
└── config_manager.cpp/.h # ⚙️ Configurações dinâmicas
```

## 📊 Diagrama de Estados

```mermaid
flowchart TD
    %% Estados principais
    BOOT[🔄 BOOT<br/>Inicialização<br/>Detecção da Base]
    AT_BASE[🏠 AT_BASE<br/>Conectado BLE<br/>Sincronização]
    SCANNING[📡 SCANNING<br/>Coletando WiFi<br/>Procurando Base]
    LOW_POWER[🔋 LOW_POWER<br/>Economia de Energia<br/>Scans Reduzidos]
    DEEP_SLEEP[💤 DEEP_SLEEP<br/>Hibernação Profunda<br/>Wake-up Timer]

    %% Transições principais
    BOOT --> AT_BASE
    BOOT --> SCANNING
    AT_BASE --> SCANNING
    SCANNING --> AT_BASE
    SCANNING --> LOW_POWER
    LOW_POWER --> AT_BASE
    LOW_POWER --> DEEP_SLEEP
    DEEP_SLEEP --> BOOT

    %% Condições das transições
    BOOT -.->|Base encontrada| AT_BASE
    BOOT -.->|Base não encontrada| SCANNING
    AT_BASE -.->|Conexão perdida| SCANNING
    SCANNING -.->|Base detectada| AT_BASE
    SCANNING -.->|Bateria baixa OU<br/>Muito tempo fora| LOW_POWER
    LOW_POWER -.->|Base detectada| AT_BASE
    LOW_POWER -.->|Bateria crítica| DEEP_SLEEP
    DEEP_SLEEP -.->|Wake-up timer<br/>ou botão| BOOT

    %% Estilos
    classDef bootState fill:#e1f5fe
    classDef baseState fill:#e8f5e8
    classDef scanState fill:#fff3e0
    classDef powerState fill:#fce4ec
    classDef sleepState fill:#f3e5f5

    class BOOT bootState
    class AT_BASE baseState
    class SCANNING scanState
    class LOW_POWER powerState
    class DEEP_SLEEP sleepState
```

## 🔄 Fluxo Detalhado por Estado

### 1️⃣ BOOT (main.cpp)
```mermaid
flowchart LR
    A[Power ON] --> B[main.cpp setup]
    B --> C[config_manager loadConfig]
    C --> D[battery_monitor checkBattery]
    D --> E[ble_client scanForBase]
    E -->|Found| F[AT_BASE]
    E -->|Not Found| G[SCANNING]
```

**Arquivos Envolvidos:**
- **main.cpp**: Inicialização geral e setup do hardware
- **config_manager.cpp**: Carrega configuração local ou padrão
- **battery_monitor.cpp**: Verifica nível de bateria inicial
- **ble_client.cpp**: Scan BLE por "BPR Base Station"

### 2️⃣ AT_BASE (ble_client.cpp)
```mermaid
flowchart LR
    A[Connect BLE] --> B[ble_client sendStatus]
    B --> C[config_manager receiveConfig]
    C --> D[wifi_scanner sendBufferedData]
    D --> E[wifi_scanner clearBuffer]
    E --> F[power_manager lightSleep]
    F --> G{Still Connected?}
    G -->|Yes| F
    G -->|No| H[SCANNING]
```

**Arquivos Envolvidos:**
- **ble_client.cpp**: Gerencia conexão e comunicação BLE
- **battery_monitor.cpp**: Coleta dados de bateria para envio
- **config_manager.cpp**: Recebe e aplica configurações da base
- **wifi_scanner.cpp**: Envia dados coletados e limpa buffer
- **power_manager.cpp**: Light sleep entre operações

### 3️⃣ SCANNING (wifi_scanner.cpp)
```mermaid
flowchart LR
    A[wifi_scanner performScan] --> B[wifi_scanner saveRecords]
    B --> C[power_manager radioDelay 300ms]
    C --> D[ble_client checkForBase]
    D --> E{Base Found?}
    E -->|Yes| F[AT_BASE]
    E -->|No| G[battery_monitor checkStatus]
    G -->|OK| H[power_manager sleepBetweenScans]
    G -->|Low/Long| I[LOW_POWER]
    H --> A
```

**Arquivos Envolvidos:**
- **wifi_scanner.cpp**: Executa scans WiFi e gerencia buffer local
- **power_manager.cpp**: Delay 200-300ms entre WiFi/BLE para evitar conflito de rádio
- **ble_client.cpp**: Verifica disponibilidade da base após delay
- **battery_monitor.cpp**: Monitora bateria para decidir modo de operação

### 4️⃣ LOW_POWER (power_manager.cpp)
```mermaid
flowchart LR
    A[power_manager enterLowPower] --> B[wifi_scanner reducedFreqScan]
    B --> C[ble_client checkForBase]
    C --> D{Base Found?}
    D -->|Yes| E[AT_BASE]
    D -->|No| F[battery_monitor isCritical]
    F -->|Yes| G[DEEP_SLEEP]
    F -->|No| H[power_manager longSleep]
    H --> B
```

**Arquivos Envolvidos:**
- **power_manager.cpp**: Controla modo de baixo consumo
- **wifi_scanner.cpp**: Scans com frequência reduzida (15min)
- **ble_client.cpp**: Continua procurando base
- **battery_monitor.cpp**: Monitora nível crítico de bateria

### 5️⃣ DEEP_SLEEP (power_manager.cpp)
```mermaid
flowchart LR
    A[power_manager prepareDeepSleep] --> B[wifi_scanner saveBuffer]
    B --> C[config_manager saveState]
    C --> D[power_manager disablePeripherals]
    D --> E[power_manager enterDeepSleep]
    E --> F[Wake Up]
    F --> G[main.cpp BOOT]
```

**Arquivos Envolvidos:**
- **power_manager.cpp**: Gerencia entrada e saída do deep sleep
- **wifi_scanner.cpp**: Salva buffer de dados antes de hibernar
- **config_manager.cpp**: Salva estado atual do sistema
- **main.cpp**: Reinicialização após wake-up

## 📡 Comunicação BLE (ble_client.cpp)

### Fluxo de Sincronização
```mermaid
sequenceDiagram
    participant BLE as ble_client.cpp
    participant BAT as battery_monitor.cpp
    participant WIFI as wifi_scanner.cpp
    participant CFG as config_manager.cpp
    participant Base as Base Station
    
    BLE->>Base: Scan & Connect
    BAT->>BLE: Get battery status
    BLE->>Base: Send BikeStatus
    Base->>CFG: Send BikeConfig
    WIFI->>BLE: Get buffered records
    BLE->>Base: Send WiFi Records (batches)
    Base->>BLE: ACK
    WIFI->>WIFI: Clear local buffer
    BLE->>Base: Disconnect or Stay Connected
```

### Estruturas de Dados
```mermaid
classDiagram
    class BikeStatus {
        +char bike_id[16]
        +float battery_voltage
        +uint32_t last_scan_timestamp
        +uint8_t flags
        +uint16_t records_count
    }
    
    class BikeConfig {
        +uint8_t version
        +uint16_t scan_interval_sec
        +uint16_t scan_interval_low_batt_sec
        +uint16_t deep_sleep_sec
        +float min_battery_voltage
        +char base_ble_name[32]
        +uint32_t timestamp
    }
    
    class WifiRecord {
        +uint32_t timestamp
        +uint8_t bssid[6]
        +int8_t rssi
        +uint8_t channel
    }
    
    class PowerState {
        +uint8_t current_state
        +uint32_t state_start_time
        +float avg_current_ma
        +uint32_t sleep_duration_ms
    }
    
    class BatteryData {
        +float voltage
        +uint8_t percentage
        +bool is_charging
        +uint32_t last_reading
    }
```

## ⚡ Gerenciamento de Energia (power_manager.cpp)

### Consumo por Estado
```mermaid
graph LR
    A[AT_BASE ~5mA power_manager] --> B[SCANNING ~50mA wifi_scanner]
    B --> C[LOW_POWER ~2mA power_manager]
    C --> D[DEEP_SLEEP ~10µA power_manager]
    D --> A
```

### Otimizações (power_manager.cpp)
- **CPU Frequency**: 80MHz (BLE) / 160MHz (WiFi)
- **WiFi TX Power**: Reduzida para -1dBm
- **BLE Parameters**: Intervalo otimizado (12ms)
- **Radio Coordination**: Delay 200-300ms entre WiFi scan e BLE scan
- **Sleep Modes**: Light sleep entre operações, deep sleep para hibernação
- **Dynamic Scaling**: Ajuste automático baseado na bateria

## 🔧 Configurações Dinâmicas (config_manager.cpp)

Todas as configurações são recebidas da Base via BLE e gerenciadas pelo config_manager.cpp:

| Parâmetro | Padrão | Descrição |
|-----------|--------|-----------|
| `scan_interval_sec` | 300s | Intervalo normal de scan |
| `scan_interval_low_batt_sec` | 900s | Intervalo em economia |
| `deep_sleep_sec` | 3600s | Duração do deep sleep |
| `min_battery_voltage` | 3.45V | Threshold bateria baixa |
| `base_ble_name` | "BPR Base Station" | Nome da base BLE |

## 🚨 Tratamento de Erros

### Modo Emergência (main.cpp)
- **Trigger**: Botão BOOT pressionado
- **Ações**: Pausa operação, menu serial
- **Opções**: Restart ('r') ou Continue ('c')

### Recuperação Automática
- **BLE Fail** (ble_client.cpp): Volta para SCANNING
- **WiFi Fail** (wifi_scanner.cpp): Retry com delay
- **Battery Critical** (battery_monitor.cpp): DEEP_SLEEP forçado
- **Memory Full** (wifi_scanner.cpp): Sobrescreve registros antigos

## 📊 Monitoramento (main.cpp)

### Status Periódico (30s)
```
==================================================
🚲 bike_001 | Estado: SCANNING | Uptime: 1234s
🔋 3.82V (85%) ✅ | 📡 42 registros
🔵 BLE: Desconectado | ⏱️ Último scan: 120s atrás
==================================================
```

### Dados Coletados por Módulo
- **battery_monitor.cpp**: Tensão, percentual, status de carregamento
- **wifi_scanner.cpp**: Redes detectadas, RSSI, timestamps
- **ble_client.cpp**: Status de conexão, última sincronização
- **power_manager.cpp**: Estado atual, tempo em cada modo
- **config_manager.cpp**: Versão da configuração, última atualização

### Indicadores LED (main.cpp)
- **Boot**: 3 piscadas rápidas
- **AT_BASE**: LED fixo
- **SCANNING**: Piscada a cada scan
- **LOW_POWER**: Piscada lenta
- **DEEP_SLEEP**: LED off

## 🔄 Integração entre Módulos

### Fluxo de Dados entre Arquivos
```mermaid
graph TD
    A[main.cpp Loop Principal] --> B[wifi_scanner.cpp Coleta WiFi]
    A --> C[ble_client.cpp Comunicação]
    A --> D[battery_monitor.cpp Monitor Bateria]
    A --> E[power_manager.cpp Gerência Energia]
    A --> F[config_manager.cpp Configurações]
    
    B --> E
    E --> C
    D --> E
    F --> B
    F --> C
    F --> E
    
    C --> G[Base Station]
    G --> C
```

### ⚡ Coordenação de Rádio (ESP32-C3)
**Consideração Técnica Importante:**
- **WiFi + BLE simultâneo**: Pode causar interferência no ESP32-C3
- **Solução implementada**: Delay de 200-300ms entre WiFi scan e BLE scan
- **Gerenciado por**: power_manager.cpp coordena o uso sequencial dos rádios
- **Benefício**: Evita conflitos de RF mantendo ambas funcionalidades ativas

### Dependências entre Módulos
- **main.cpp**: Orquestra todos os outros módulos
- **config_manager.cpp**: Fornece configurações para todos
- **battery_monitor.cpp**: Informa power_manager.cpp sobre estado da bateria
- **wifi_scanner.cpp**: Usa configurações e coordena com power_manager para timing
- **ble_client.cpp**: Aguarda sinal do power_manager após WiFi scan
- **power_manager.cpp**: Controla energia E coordenação de rádio entre WiFi/BLE