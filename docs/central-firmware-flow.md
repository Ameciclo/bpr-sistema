# Central Firmware - Fluxo Completo

Este documento descreve o funcionamento completo do firmware da central BPR como **Hub Inteligente**, incluindo gerenciamento de múltiplas bicicletas e sistema de configuração bidirecional.

## 1. 🔄 Fluxo Principal dos Modos (Atualizado)

```mermaid
graph TD
    A[🚀 SETUP] --> B[Inicializar LittleFS]
    B --> C{Primeira vez?}
    C -->|Sim| D[Modo SETUP_AP]
    D --> E[Criar AP: BPR_Setup_XXXXXX]
    E --> F[Interface Web: 192.168.4.1]
    F --> G[Configurar: Base ID, WiFi, Firebase]
    G --> H[Salvar config.json]
    H --> I[Reiniciar ESP32]
    I --> A
    
    C -->|Não| J[Carregar Config]
    J --> K[Inicializar Bike Manager]
    K --> L[Carregar Cache Config]
    L --> M{Config Válida?}
    M -->|Não| N[Marcar para Download]
    M -->|Sim| O[Inicializar BLE Server]
    N --> O
    O --> P[Modo BLE_ONLY]
    
    P --> Q{"Precisa Sync?<br/>Dados ou 5min ou Config inválida"}
    Q -->|Não| R[Processar Configs Pendentes]
    R --> S[Limpeza Conexões 1min]
    S --> T[Aguardar dados BLE]
    T --> U[Bike conecta/envia dados]
    U --> V[Registrar/Atualizar bike]
    V --> W[Enviar config se necessário]
    W --> Q
    Q -->|Sim| X[Ativar modo WIFI_SYNC]
    
    X --> Y[Conectar WiFi]
    Y --> Z{WiFi conectado?}
    Z -->|Não| AA{Timeout 30s?}
    AA -->|Não| Z
    AA -->|Sim| BB[Modo SHUTDOWN]
    
    BB --> CC[Desconectar WiFi]
    CC --> DD[WiFi.mode OFF]
    DD --> P
    
    classDef modeClass fill:#e1f5fe,stroke:#01579b,stroke-width:2px
    classDef processClass fill:#f3e5f5,stroke:#4a148c,stroke-width:2px
    classDef configClass fill:#e8f5e8,stroke:#1b5e20,stroke-width:2px
    classDef setupClass fill:#fff3e0,stroke:#e65100,stroke-width:2px
    classDef errorClass fill:#ffebee,stroke:#b71c1c,stroke-width:2px
    
    class P,X,BB modeClass
    class B,K,Y processClass
    class J,L,M,R,V,W configClass
    class D,E,F,G,H setupClass
    class AA errorClass
```

## 2. 🔄 Sincronização Completa (NTP + Config + Upload)

```mermaid
graph TD
    A[WiFi Conectado] --> B{NTP sincronizado?}
    B -->|Não| C[Sincronizar NTP]
    C --> D{NTP OK?}
    D -->|Sim| E[Salvar epoch + millis base]
    D -->|Não| F[Usar millis como fallback]
    E --> G[Preparar correção NTP para bikes]
    F --> H{Tem dados pendentes?}
    G --> H
    B -->|Sim| H
    
    H -->|Sim| I{"Dados maior que 8KB?"}
    I -->|Não| J[Upload direto Firebase]
    I -->|Sim| K[Dividir em batches]
    K --> L[Upload batch por batch]
    L --> M{Mais batches?}
    M -->|Sim| L
    M -->|Não| N[Limpar pendingData]
    J --> O{Upload OK?}
    O -->|Sim| N
    O -->|Não| P[Manter dados pendentes]
    N --> Q[Modo SHUTDOWN]
    P --> Q
    H -->|Não| Q
    
    classDef processClass fill:#f3e5f5,stroke:#4a148c,stroke-width:2px
    classDef dataClass fill:#e8f5e8,stroke:#1b5e20,stroke-width:2px
    classDef errorClass fill:#ffebee,stroke:#b71c1c,stroke-width:2px
    
    class C,E,F,G,J,K,L processClass
    class N dataClass
    class P errorClass
```

## 3. 🚲 Gerenciamento de Múltiplas Bikes

```mermaid
sequenceDiagram
    participant B1 as Bike 01
    participant B2 as Bike 07
    participant C as Central
    participant F as Firebase
    
    Note over C: Central sempre em BLE
    
    B1->>C: Conecta BLE (handle: 1)
    C->>C: Registra bike01 + marca config
    B1->>C: Envia dados status
    C->>C: Identifica bike01
    C->>B1: Envia configuração
    
    B2->>C: Conecta BLE (handle: 2)
    C->>C: Registra bike07 + marca config
    B2->>C: Envia WiFi scan
    C->>C: Identifica bike07
    C->>B2: Envia configuração
    
    Note over C: Acumula dados de ambas
    
    C->>C: Trigger sync (5min/dados/config)
    C->>F: Download configs
    C->>F: Upload dados bike01+bike07
    F-->>C: Confirmação
    C->>C: Marca bikes para reconfig
    
    B1->>C: Próxima conexão
    C->>B1: Nova configuração
```

## 4. ⚙️ Sistema de Configuração Bidirecional

```mermaid
graph TD
    A[Central Liga] --> B[Carrega Cache Local]
    B --> C{"Cache Válido?<br/>(menor que 1h)"}
    C -->|Sim| D[Usa Config Cached]
    C -->|Não| E[Marca Download Necessário]
    
    E --> F[Próxima Sync WiFi]
    F --> G[Download /config + /bases/ameciclo]
    G --> H{Download OK?}
    H -->|Sim| I[Atualiza Cache]
    H -->|Não| J[Usa Padrões]
    I --> K[Marca Todas Bikes Reconfig]
    J --> K
    
    D --> L[Bike Conecta]
    K --> L
    L --> M{Bike Precisa Config?}
    M -->|Sim| N[Envia BPRConfigPacket]
    M -->|Não| O[Só Recebe Dados]
    N --> P[Marca Config Enviada]
    P --> O
    
    classDef cacheClass fill:#e8f5e8,stroke:#1b5e20,stroke-width:2px
    classDef downloadClass fill:#e1f5fe,stroke:#01579b,stroke-width:2px
    classDef configClass fill:#fff3e0,stroke:#e65100,stroke-width:2px
    
    class B,C,D,I cacheClass
    class F,G,H downloadClass
    class L,M,N,P configClass
```

## 5. 🕰️ Correção de Timestamps

```mermaid
graph TD
    A[Bike envia timestamp] --> B{Timestamp > 2020?}
    B -->|Sim| C[Usar timestamp da bike]
    B -->|Não| D{Central tem NTP?}
    D -->|Sim| E[Corrigir com NTP central]
    D -->|Não| F[Usar timestamp original]
    E --> G[Timestamp corrigido]
    C --> G
    F --> G
    
    classDef validClass fill:#e8f5e8,stroke:#1b5e20,stroke-width:2px
    classDef correctionClass fill:#fff3e0,stroke:#e65100,stroke-width:2px
    classDef fallbackClass fill:#ffebee,stroke:#b71c1c,stroke-width:2px
    
    class C,G validClass
    class E correctionClass
    class F fallbackClass
```

## 6. 🔧 Modo Setup AP (Novo)

```mermaid
sequenceDiagram
    participant U as Usuário
    participant C as Central
    participant W as WebServer
    participant F as Firebase
    
    Note over C: Primeira execução detectada
    
    C->>C: Criar AP: BPR_Setup_XXXXXX
    C->>W: Iniciar servidor web
    C->>C: LED pisca alternado
    
    U->>C: Conecta WiFi no AP
    U->>W: Acessa http://192.168.4.1
    W->>U: Interface de configuração
    
    U->>W: Preenche formulário
    Note over U,W: Base ID, WiFi, Firebase, API Key
    
    W->>C: Salva config.json
    W->>C: Salva firebase_config.json
    C->>U: Confirmação de sucesso
    C->>C: Reinicia ESP32
    
    Note over C: Próxima inicialização
    
    C->>C: Carrega config.json
    C->>F: Tenta baixar config completa
    F-->>C: Config existente OU 404
    
    alt Config existe
        C->>C: Aplica config baixada
    else Config não existe
        C->>F: Cria nova base
        F-->>C: Base criada
    end
    
    C->>C: Modo BLE normal
```

## 7. 📊 Estados e Configurações (Atualizados)

### Estados Globais
```cpp
// Modos de operação
currentMode: SETUP_AP | BLE_ONLY | WIFI_SYNC | SHUTDOWN

// Variáveis do modo setup
WebServer setupServer(80);
bool setupComplete = false;

// Dados e sincronização
pendingData: String com JSONs acumulados
lastSync: timestamp do último sync

// NTP e correção temporal
ntpSynced: bool se NTP está válido
ntpEpoch: timestamp NTP de referência
ntpMillisBase: millis() de referência

// Gerenciamento de bikes
std::vector<ConnectedBike> connectedBikes
ConfigCache configCache (global + base)
```

### Estruturas de Dados
```cpp
struct ConnectedBike {
    char bikeId[8];
    uint16_t connHandle;
    bool configSent;
    bool needsConfig;
    uint32_t lastSeen;
    float lastBattery;
};

struct ConfigCache {
    GlobalConfig global;
    BaseConfig base;
    uint32_t lastUpdate;
    bool valid; // Válido por 1h
};
```

### Configurações Firebase
```json
// GET /config.json
{
  "version": 3,
  "wifi_scan_interval_sec": 25,
  "wifi_scan_interval_low_batt_sec": 60,
  "deep_sleep_after_sec": 300,
  "ble_ping_interval_sec": 5,
  "min_battery_voltage": 3.45,
  "update_timestamp": 1764782576
}

// GET /bases/ameciclo.json
{
  "name": "Ameciclo",
  "max_bikes": 10,
  "wifi_ssid": "BPR_Base",
  "wifi_password": "botaprarodar6",
  "location": {"lat": -8.062, "lng": -34.881},
  "last_sync": 1764782576
}
```

### Estrutura Firebase Upload
```json
// PUT /central_data/{timestamp}.json
{
  "timestamp": 1764782576,
  "data": [
    {"type": "bike", "data": {...}},
    {"type": "wifi", "data": {...}},
    {"type": "alert", "data": {...}},
    {"type": "ntp_sync", "epoch": 1764782576, "millis": 123456}
  ]
}
```

## 📋 Resumo do Funcionamento

### 🔄 Ciclo Principal
1. **Modo SETUP_AP** (primeira vez): Interface web para configuração inicial
2. **Modo BLE_ONLY** (padrão): Recebe dados das bicicletas via BLE
3. **Modo WIFI_SYNC** (temporário): Conecta WiFi e sincroniza com Firebase
4. **Modo SHUTDOWN**: Desliga WiFi e volta ao BLE

### 🔧 Setup Inicial
- Detecta primeira execução (sem config.json)
- Cria AP com interface web para configuração
- Baixa ou cria configuração completa no Firebase
- LED indica visualmente cada estado

### 🕰️ Sincronização Temporal
- Central sincroniza NTP quando conecta WiFi
- Corrige timestamps das bicicletas que não têm NTP válido
- Envia correção temporal para bicicletas via BLE

### 📦 Gestão de Dados
- Acumula dados das bicicletas em `pendingData`
- Divide uploads grandes (>8KB) em batches
- Upload direto HTTPS para Firebase (sem bibliotecas extras)

### 🔋 Eficiência Energética
- WiFi fica desligado na maior parte do tempo
- Ativa WiFi apenas para sincronização (a cada 5min ou quando há dados)
- Timeout de 30s para conexões WiFi

### 🛡️ Robustez
- Fallbacks para timestamps sem NTP
- Retry automático em caso de falha de upload
- Logs detalhados para debugging
- Gestão de memória com monitoramento de heap