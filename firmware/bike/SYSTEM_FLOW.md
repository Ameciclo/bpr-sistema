# BPR Bike System - Fluxo de Funcionamento

## 🎯 Visão Geral

Sistema de bicicleta compartilhada com comunicação BLE e coleta de dados WiFi para geolocalização offline.

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

### 1️⃣ BOOT (Inicialização)
```mermaid
flowchart LR
    A[Power ON] --> B[Init Hardware]
    B --> C[Load Config]
    C --> D[Check Battery]
    D --> E{Scan for Base}
    E -->|Found| F[AT_BASE]
    E -->|Not Found| G[SCANNING]
```

**Ações:**
- Inicializar hardware (LED, botão, ADC)
- Carregar configuração local
- Verificar nível de bateria
- Scan BLE por "BPR Base Station"

### 2️⃣ AT_BASE (Na Base)
```mermaid
flowchart LR
    A[Connect BLE] --> B[Send Status]
    B --> C[Receive Config]
    C --> D[Send WiFi Data]
    D --> E[Clear Buffer]
    E --> F[Light Sleep 1min]
    F --> G{Still Connected?}
    G -->|Yes| F
    G -->|No| H[SCANNING]
```

**Ações:**
- Conectar BLE à base
- Enviar status (bateria, registros)
- Receber configurações atualizadas
- Transmitir dados WiFi coletados
- Light sleep periódico (1 minuto)

### 3️⃣ SCANNING (Coletando Dados)
```mermaid
flowchart LR
    A[WiFi Scan] --> B[Save Records]
    B --> C[Check for Base]
    C --> D{Base Found?}
    D -->|Yes| E[AT_BASE]
    D -->|No| F{Battery/Time Check}
    F -->|OK| G[Sleep & Repeat]
    F -->|Low/Long| H[LOW_POWER]
    G --> A
```

**Ações:**
- Scan WiFi periódico (5min padrão)
- Salvar registros (BSSID, RSSI, timestamp)
- Procurar base a cada ciclo
- Light sleep entre scans

### 4️⃣ LOW_POWER (Economia)
```mermaid
flowchart LR
    A[Reduce Scan Freq] --> B[WiFi Scan 15min]
    B --> C[Check for Base]
    C --> D{Base Found?}
    D -->|Yes| E[AT_BASE]
    D -->|No| F{Battery Critical?}
    F -->|Yes| G[DEEP_SLEEP]
    F -->|No| H[Long Sleep]
    H --> B
```

**Ações:**
- Scans menos frequentes (15min)
- Procurar base continuamente
- Long light sleep entre operações

### 5️⃣ DEEP_SLEEP (Hibernação)
```mermaid
flowchart LR
    A[Save Critical Data] --> B[Disable All]
    B --> C[Set Wake Timer]
    C --> D[Deep Sleep]
    D --> E[Wake Up]
    E --> F[BOOT]
```

**Ações:**
- Salvar dados críticos
- Desabilitar WiFi/BLE
- Configurar wake-up timer (1h padrão)
- Entrar em deep sleep (<10µA)

## 📡 Comunicação BLE

### Fluxo de Sincronização
```mermaid
sequenceDiagram
    participant B as Bike
    participant Base as Base Station
    
    B->>Base: Scan & Connect
    B->>Base: Send BikeStatus
    Base->>B: Send BikeConfig
    B->>Base: Send WiFi Records (batches)
    Base->>B: ACK
    B->>B: Clear local buffer
    B->>Base: Disconnect or Stay Connected
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
```

## ⚡ Gerenciamento de Energia

### Consumo por Estado
```mermaid
graph LR
    A[AT_BASE<br/>~5mA] --> B[SCANNING<br/>~50mA]
    B --> C[LOW_POWER<br/>~2mA]
    C --> D[DEEP_SLEEP<br/>~10µA]
    D --> A
```

### Otimizações
- **CPU Frequency**: 80MHz (BLE) / 160MHz (WiFi)
- **WiFi TX Power**: Reduzida para -1dBm
- **BLE Parameters**: Intervalo otimizado (12ms)
- **Sleep Modes**: Light sleep entre operações, deep sleep para hibernação

## 🔧 Configurações Dinâmicas

Todas as configurações são recebidas da Base via BLE:

| Parâmetro | Padrão | Descrição |
|-----------|--------|-----------|
| `scan_interval_sec` | 300s | Intervalo normal de scan |
| `scan_interval_low_batt_sec` | 900s | Intervalo em economia |
| `deep_sleep_sec` | 3600s | Duração do deep sleep |
| `min_battery_voltage` | 3.45V | Threshold bateria baixa |
| `base_ble_name` | "BPR Base Station" | Nome da base BLE |

## 🚨 Tratamento de Erros

### Modo Emergência
- **Trigger**: Botão BOOT pressionado
- **Ações**: Pausa operação, menu serial
- **Opções**: Restart ('r') ou Continue ('c')

### Recuperação Automática
- **BLE Fail**: Volta para SCANNING
- **WiFi Fail**: Retry com delay
- **Battery Critical**: DEEP_SLEEP forçado
- **Memory Full**: Sobrescreve registros antigos

## 📊 Monitoramento

### Status Periódico (30s)
```
==================================================
🚲 bike_001 | Estado: SCANNING | Uptime: 1234s
🔋 3.82V (85%) ✅ | 📡 42 registros
🔵 BLE: Desconectado | ⏱️ Último scan: 120s atrás
==================================================
```

### Indicadores LED
- **Boot**: 3 piscadas rápidas
- **AT_BASE**: LED fixo
- **SCANNING**: Piscada a cada scan
- **LOW_POWER**: Piscada lenta
- **DEEP_SLEEP**: LED off