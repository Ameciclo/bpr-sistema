# 🚲 BPR Bike System - Fluxo Completo

Sistema de firmware para bicicletas compartilhadas com comunicação BLE e coleta de dados WiFi.

## 🎯 Visão Geral

A bicicleta opera em **5 estados principais** com foco em **ultra baixo consumo** e **comunicação BLE** com a central.

## 📊 Máquina de Estados Principal

```mermaid
flowchart TD
    %% Estados principais
    BOOT[🔄 BOOT<br/>Inicialização<br/>Detecção da Central]
    AT_BASE[🏠 AT_BASE<br/>Conectado BLE<br/>Sincronização]
    SCANNING[📡 SCANNING<br/>Coletando WiFi<br/>Procurando Central]
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
    BOOT -.->|Central encontrada<br/>BPR Base Station| AT_BASE
    BOOT -.->|Central não encontrada<br/>Timeout 5s| SCANNING
    AT_BASE -.->|Conexão BLE perdida<br/>ou timeout| SCANNING
    SCANNING -.->|Central detectada<br/>via BLE scan| AT_BASE
    SCANNING -.->|Bateria baixa OU<br/>Tempo maior 1h| LOW_POWER
    LOW_POWER -.->|Central detectada| AT_BASE
    LOW_POWER -.->|Bateria crítica<br/>menor 3.35V| DEEP_SLEEP
    DEEP_SLEEP -.->|Wake-up timer<br/>ou botão BOOT| BOOT

    %% Estilos
    classDef bootState fill:#e1f5fe,stroke:#01579b,stroke-width:2px
    classDef baseState fill:#e8f5e8,stroke:#1b5e20,stroke-width:2px
    classDef scanState fill:#fff3e0,stroke:#e65100,stroke-width:2px
    classDef powerState fill:#fce4ec,stroke:#880e4f,stroke-width:2px
    classDef sleepState fill:#f3e5f5,stroke:#4a148c,stroke-width:2px

    class BOOT bootState
    class AT_BASE baseState
    class SCANNING scanState
    class LOW_POWER powerState
    class DEEP_SLEEP sleepState
```

## 🔄 Fluxo Detalhado por Estado

### 1️⃣ Estado BOOT (Inicialização)

```mermaid
flowchart LR
    A[Power ON] --> B[Init Hardware<br/>LED, Botão, ADC]
    B --> C[Init LittleFS<br/>Config Manager]
    C --> D[Load Config<br/>JSON ou Padrões]
    D --> E[Init BLE Client<br/>BPR_Bike_bike_001]
    E --> F[Check Battery<br/>ADC + Média Móvel]
    F --> G{Scan BLE<br/>5s timeout}
    G -->|BPR Base Station<br/>encontrada| H[AT_BASE]
    G -->|Timeout ou<br/>nao encontrada| I[SCANNING]
```

**Ações Específicas:**
- Inicializar hardware (LED pino 8, botão pino 9, ADC)
- Carregar configuração JSON ou usar padrões
- Configurar BLE como cliente `BPR_Bike_bike_001`
- Verificar nível de bateria com média móvel
- Scan BLE ativo por 5 segundos

### 2️⃣ Estado AT_BASE (Na Central)

```mermaid
flowchart LR
    A[BLE Scan + Connect] --> B[Register with Base<br/>JSON registration]
    B --> C[Send Bike Info<br/>Status + Battery]
    C --> D[Receive Config<br/>JSON via BLE]
    D --> E[Send WiFi Data<br/>Batch upload]
    E --> F[Clear Local Buffer<br/>LittleFS cleanup]
    F --> G[Light Sleep 1min<br/>Manter conexão]
    G --> H{Still Connected?}
    H -->|Yes| G
    H -->|No| I[SCANNING]
```

**Dados Enviados:**
```json
{
  "type": "status",
  "bike_id": "bike_001",
  "battery_voltage": 4.66,
  "records_count": 0,
  "timestamp": 123456,
  "heap": 174332
}
```

**Configuração Recebida:**
```json
{
  "scan_interval_sec": 300,
  "scan_interval_low_batt_sec": 900,
  "deep_sleep_sec": 3600,
  "min_battery_voltage": 3.45,
  "base_ble_name": "BPR Base Station"
}
```

### 3️⃣ Estado SCANNING (Coletando Dados)

```mermaid
flowchart LR
    A[WiFi Scan<br/>Ativo 300ms] --> B[Save Records<br/>BSSID + RSSI + CH]
    B --> C[Check for Base<br/>BLE scan passivo]
    C --> D{Base Found?}
    D -->|Yes| E[AT_BASE]
    D -->|No| F{Battery/Time Check}
    F -->|Battery OK<br/>Time menor 1h| G[Light Sleep<br/>75s cycle]
    F -->|Battery Low OR<br/>Time maior 1h| H[LOW_POWER]
    G --> A
```

**Registro WiFi:**
```cpp
struct WifiRecord {
  uint32_t timestamp;
  uint8_t bssid[6];
  int8_t rssi;
  uint8_t channel;
};
```

**Sistema de Armazenamento:**
- **Buffer RAM**: 50 registros (flush automático)
- **Arquivos LittleFS**: 1000 registros por arquivo
- **Capacidade total**: ~20.000 registros (1MB flash)
- **Autonomia**: ~14 dias de coleta contínua
- **Estrutura**: `/wifi_0.json`, `/wifi_1.json`, etc.

**Otimizações:**
- CPU: 160MHz (para WiFi)
- WiFi TX Power: -1dBm (economia)
- Scan limitado a 20 redes mais fortes
- Flush automático RAM → Flash

### 4️⃣ Estado LOW_POWER (Economia)

```mermaid
flowchart LR
    A[Reduce Scan Freq<br/>15min intervals] --> B[WiFi Scan<br/>Menos agressivo]
    B --> C[Check for Base<br/>BLE scan]
    C --> D{Base Found?}
    D -->|Yes| E[AT_BASE]
    D -->|No| F{Battery Critical?}
    F -->|maior 3.35V| G[Long Sleep<br/>15min cycle]
    F -->|menor 3.35V| H[DEEP_SLEEP]
    G --> A
```

**Características:**
- Scans WiFi a cada 15 minutos
- CPU reduzida para 80MHz
- Light sleep longo entre operações
- Monitoramento contínuo de bateria

### 5️⃣ Estado DEEP_SLEEP (Hibernação)

```mermaid
flowchart LR
    A[Save Critical Data<br/>Config + Estado] --> B[Disable All<br/>WiFi + BLE off]
    B --> C[Set Wake Timer<br/>1h padrão]
    C --> D[Deep Sleep<br/>< 10µA]
    D --> E[Wake Up<br/>Timer ou Botão]
    E --> F[BOOT]
```

**Wake-up Sources:**
- Timer RTC (1 hora padrão)
- Botão BOOT (GPIO 9)
- Consumo: < 10µA

## 📡 Comunicação BLE com Central

### Fluxo de Conexão

```mermaid
sequenceDiagram
    participant B as Bicicleta
    participant C as Central
    
    Note over B: Estado BOOT ou SCANNING
    
    B->>B: BLE Scan ativo
    B->>B: Procura "BPR Base Station"
    C->>C: Advertising BLE ativo
    
    B->>C: Conectar (padrão simulator)
    C->>B: Conexão aceita
    
    B->>C: Register JSON
    B->>C: Bike Info JSON
    B->>C: Status JSON
    C->>B: Config JSON
    B->>C: WiFi Data JSON (se houver)
    C->>C: Processar e cachear
    
    Note over B,C: Manter conexão com light sleep
    
    alt Conexão perdida
        B->>B: Detectar desconexão
        B->>B: Mudar para SCANNING
    else Dados enviados
        B->>B: Light sleep 1min
        B->>C: Verificar conexão
    end
```

### Características BLE

| Parâmetro | Valor | Descrição |
|-----------|-------|-----------|
| **Service UUID** | `BAAD` | Serviço principal |
| **Config Char** | `F00D` | Central → Bike (configs) |
| **Status Char** | `BEEF` | Bike → Central (status) |
| **Data Char** | `CAFE` | Bike → Central (dados WiFi) |
| **Scan Interval** | 100ms | Intervalo de scan |
| **Scan Window** | 99ms | Janela ativa |
| **Scan Type** | Ativo | Para descoberta rápida |

## ⚡ Gerenciamento de Energia

### Consumo por Estado

```mermaid
graph LR
    A[AT_BASE<br/>aprox 5mA<br/>BLE ativo] --> B[SCANNING<br/>aprox 50mA<br/>WiFi + BLE]
    B --> C[LOW_POWER<br/>aprox 2mA<br/>Intervalos longos]
    C --> D[DEEP_SLEEP<br/>aprox 10uA<br/>Hibernacao]
    D --> A
    
    style A fill:#e8f5e8
    style B fill:#fff3e0
    style C fill:#fce4ec
    style D fill:#f3e5f5
```

### Otimizações Implementadas

```cpp
// Configurações de energia
setCpuFrequencyMhz(80);           // BLE mode
setCpuFrequencyMhz(160);          // WiFi mode
WiFi.setTxPower(WIFI_POWER_7dBm); // Potência reduzida
btStop();                         // Bluetooth clássico off

// Sleep modes
powerManager.enterLightSleep(75); // Entre scans
powerManager.enterDeepSleep(3600); // Hibernação
```

## 🔧 Sistema de Configuração

### Configuração JSON Local

```json
{
  "bike_id": "bike_001",
  "base_ble_name": "BPR Base Station",
  "scan_interval_sec": 300,
  "scan_interval_low_batt_sec": 900,
  "deep_sleep_sec": 3600,
  "min_battery_voltage": 3.45,
  "max_wifi_records": 200,
  "ble_scan_timeout_sec": 5,
  "emergency_timeout_sec": 10,
  "status_report_interval_sec": 30,
  "led_enabled": true,
  "debug_enabled": true
}
```

### Atualização Dinâmica via BLE

```mermaid
sequenceDiagram
    participant B as Bicicleta
    participant C as Central
    participant F as Firebase
    
    C->>F: Download config atualizada
    F-->>C: Nova configuração
    
    B->>C: Conectar BLE
    C->>B: Enviar nova config JSON
    B->>B: Atualizar ConfigManager
    B->>B: Salvar em LittleFS
    B->>C: Confirmar recebimento
```

## 🚨 Sistema de Alertas e Emergência

### Modo Emergência (Botão BOOT)

```mermaid
flowchart TD
    A[Botão BOOT Pressionado] --> B[Interromper Estado Atual]
    B --> C[Mostrar Menu Serial]
    C --> D{Comando?}
    D -->|'r'| E[ESP.restart()]
    D -->|'c'| F[Continuar operação]
    D -->|Timeout 10s| F
```

### Alertas de Bateria

```cpp
// Níveis de bateria
if (voltage < 3.35) {
    // Crítico - Deep sleep forçado
    changeState(DEEP_SLEEP);
} else if (voltage < 3.45) {
    // Baixo - Modo economia
    changeState(LOW_POWER);
}
```

## 📊 Monitoramento e Debug

### Status Periódico (30s)

```
==================================================
🚲 bike_001 | Estado: SCANNING | Uptime: 1234s
🔋 3.82V (85%) ✅ | 📡 42 registros
🔵 BLE: Desconectado | ⏱️ Último scan: 120s atrás
==================================================
```

### Indicadores LED (Pino 8)

| Estado | Padrão LED | Descrição |
|--------|------------|-----------|
| **BOOT** | 3 piscadas rápidas | Inicializando |
| **AT_BASE** | LED fixo | Conectado na central |
| **SCANNING** | Piscada a cada scan | Coletando dados |
| **LOW_POWER** | Piscada lenta | Modo economia |
| **DEEP_SLEEP** | LED off | Hibernação |

## 🔄 Ciclo de Vida Típico

### Cenário: Bicicleta na Base

```mermaid
gantt
    title Ciclo de Vida - Bicicleta na Base
    dateFormat X
    axisFormat %M:%S
    
    section Estados
    BOOT           :0, 10
    AT_BASE        :10, 70
    Light Sleep    :70, 130
    AT_BASE        :130, 190
    Light Sleep    :190, 250
```

### Cenário: Bicicleta em Viagem

```mermaid
gantt
    title Ciclo de Vida - Bicicleta em Viagem
    dateFormat X
    axisFormat %H:%M
    
    section Estados
    BOOT           :0, 1
    SCANNING       :1, 60
    Light Sleep    :60, 65
    SCANNING       :65, 120
    LOW_POWER      :120, 180
    DEEP_SLEEP     :180, 240
```

## 🛠️ Troubleshooting

### Problemas Comuns

1. **Central não encontrada**
   - Verificar nome BLE: `"BPR Base Station"`
   - Verificar alcance BLE (< 10m)
   - Verificar se central está em modo BLE

2. **Conexão BLE instável**
   - Verificar interferências 2.4GHz
   - Verificar qualidade do sinal (RSSI)
   - Verificar se central não está em modo WiFi

3. **Bateria drena rápido**
   - Verificar se está entrando em sleep
   - Verificar configurações de intervalo
   - Verificar se WiFi está desligando

4. **Dados não são enviados**
   - Verificar UUIDs BLE
   - Verificar formato JSON
   - Verificar se central processa dados

### Comandos de Debug

```cpp
// Menu serial (pressionar 'm')
- Status completo do sistema
- Configurações atuais
- Estado da bateria
- Contagem de registros WiFi
- Status BLE
```

## 📈 Métricas de Performance

### Autonomia Estimada

| Cenário | Consumo Médio | Autonomia (3000mAh) |
|---------|---------------|---------------------|
| **Na Base** | 5mA | ~25 dias |
| **Viagem Normal** | 15mA | ~8 dias |
| **Viagem Economia** | 8mA | ~15 dias |
| **Deep Sleep** | 0.01mA | ~8 meses |

### Eficiência de Dados

- **Registro WiFi**: ~50 bytes (JSON)
- **Buffer RAM**: 50 registros (2.5KB)
- **Capacidade flash**: 20.000 registros (1MB)
- **Arquivos**: 1000 registros por arquivo JSON
- **Upload**: Export completo via BLE
- **Limpeza**: Automática após upload bem-sucedido

### Fluxo de Armazenamento

```mermaid
flowchart LR
    A[WiFi Scan] --> B[Buffer RAM<br/>50 registros]
    B --> C{Buffer cheio?}
    C -->|Sim| D[Flush → /wifi_X.json<br/>1000 registros/arquivo]
    C -->|Não| B
    D --> E[Na base: Export tudo]
    E --> F[Upload via BLE]
    F --> G[Limpar arquivos]
    G --> B
```

---

## 🎯 Resumo Executivo

O firmware da bicicleta BPR é um sistema **ultra-eficiente** que:

- ✅ **Opera 5 estados** com transições inteligentes
- ✅ **Comunica via BLE** com a central usando padrão testado
- ✅ **Coleta dados WiFi** para geolocalização offline
- ✅ **Gerencia energia** com múltiplos níveis de economia
- ✅ **Configura dinamicamente** via JSON da central
- ✅ **Monitora bateria** com alertas automáticos
- ✅ **Funciona offline** com buffer local robusto

**Autonomia**: 8-25 dias dependendo do uso  
**Alcance BLE**: ~10 metros da central  
**Dados coletados**: Até 20.000 registros WiFi persistentes  
**Consumo mínimo**: 10µA em deep sleep  

## 💾 Sistema de Armazenamento LittleFS

### Hardware Base
- **MCU**: Seeed Xiao ESP32C3 (4MB flash interno)
- **Partições**: Bootloader + App + OTA + **LittleFS (1MB)**
- **Capacidade**: ~20.000 registros WiFi
- **Autonomia**: ~14 dias de coleta contínua

### Estrutura de Arquivos
```
/wifi_index.txt     # Índice do arquivo atual
/wifi_0.json        # Primeiros 1000 registros  
/wifi_1.json        # Próximos 1000 registros
/wifi_N.json        # Até esgotar espaço
/config.json        # Configurações da bike
```

### Fluxo de Dados
```mermaid
flowchart TD
    A[WiFi Scan] --> B[Buffer RAM<br/>50 registros]
    B --> C{Buffer >= 50?}
    C -->|Sim| D[Flush para /wifi_X.json]
    C -->|Não| B
    D --> E{Arquivo >= 1000?}
    E -->|Sim| F[Próximo arquivo<br/>wifi_X+1.json]
    E -->|Não| G[Continuar arquivo atual]
    F --> G
    G --> H[Na base: Export tudo]
    H --> I[Upload via BLE]
    I --> J[Limpar todos arquivos]
    J --> B
```

### Vantagens vs Buffer RAM
| Aspecto | Buffer RAM (antigo) | LittleFS (novo) |
|---------|---------------------|-----------------|
| **Capacidade** | 200 registros | 20.000 registros |
| **Persistência** | ❌ Perde na reinicialização | ✅ Mantém dados |
| **Autonomia** | ~2 horas | ~14 dias |
| **Memória RAM** | 3.2KB ocupados | 2.5KB ocupados |
| **Robustez** | ❌ Frágil | ✅ Robusto |
