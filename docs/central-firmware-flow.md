# Central Firmware - Fluxo Completo

Este diagrama descreve o funcionamento completo do firmware da central BPR, incluindo os três modos de operação, sincronização NTP, correção de timestamps e upload para Firebase.

```mermaid
graph TD
    A[🚀 SETUP] --> B[Inicializar LittleFS]
    B --> C[Inicializar BLE Server]
    C --> D[Modo BLE_ONLY]
    
    %% Modo BLE Only
    D --> E{Tem dados pendentes<br/>OU 5min sem sync?}
    E -->|Não| F[Aguardar dados BLE]
    F --> G[Receber dados das bikes]
    G --> H[Adicionar a pendingData]
    H --> E
    E -->|Sim| I[Ativar modo WIFI_SYNC]
    
    %% Modo WiFi Sync
    I --> J[Conectar WiFi]
    J --> K{WiFi conectado?}
    K -->|Não| L{Timeout 30s?}
    L -->|Não| K
    L -->|Sim| M[Modo SHUTDOWN]
    
    K -->|Sim| N{NTP sincronizado?}
    N -->|Não| O[Sincronizar NTP]
    O --> P{NTP OK?}
    P -->|Sim| Q[Salvar epoch + millis base]
    P -->|Não| R[Usar millis como fallback]
    Q --> S[Preparar correção NTP para bikes]
    R --> T{Tem dados pendentes?}
    S --> T
    N -->|Sim| T
    
    %% Upload Firebase
    T -->|Sim| U{Dados > 8KB?}
    U -->|Não| V[Upload direto Firebase]
    U -->|Sim| W[Dividir em batches]
    W --> X[Upload batch por batch]
    X --> Y{Mais batches?}
    Y -->|Sim| X
    Y -->|Não| Z[Limpar pendingData]
    V --> AA{Upload OK?}
    AA -->|Sim| Z
    AA -->|Não| BB[Manter dados pendentes]
    Z --> M
    BB --> M
    T -->|Não| M
    
    %% Modo Shutdown
    M --> CC[Desconectar WiFi]
    CC --> DD[WiFi.mode OFF]
    DD --> D
    
    %% Correção de Timestamps
    subgraph "🕰️ Correção Temporal"
        E1[Bike envia timestamp]
        E2{Timestamp > 2020?}
        E1 --> E2
        E2 -->|Sim| E3[Usar timestamp da bike]
        E2 -->|Não| E4{Central tem NTP?}
        E4 -->|Sim| E5[Corrigir com NTP central]
        E4 -->|Não| E6[Usar timestamp original]
        E5 --> E7[Timestamp corrigido]
        E3 --> E7
        E6 --> E7
    end
    
    %% Estados e Variáveis
    subgraph "📊 Estados Globais"
        S1[currentMode: BLE_ONLY/WIFI_SYNC/SHUTDOWN]
        S2[pendingData: String com JSONs]
        S3[lastSync: último sync timestamp]
        S4[ntpSynced: bool NTP válido]
        S5[ntpEpoch: timestamp NTP]
        S6[ntpMillisBase: millis de referência]
    end
    
    %% Funções Principais
    subgraph "🔧 Funções Principais"
        F1[correctTimestamp - Corrige timestamps das bikes]
        F2[sendNTPToBike - Envia correção via BLE]
        F3[uploadToFirebase - Upload HTTPS direto]
        F4[handleBLEMode - Gerencia modo BLE]
        F5[handleWiFiMode - Gerencia sync WiFi]
        F6[handleShutdownMode - Desliga WiFi]
    end
    
    %% Loop Principal
    subgraph "🔄 Loop Principal"
        L1[Switch currentMode]
        L2[Log periódico 15s]
        L3[Delay 100ms]
        L1 --> L2
        L2 --> L3
        L3 --> L1
    end
    
    %% Integração Firebase
    subgraph "🔥 Estrutura Firebase"
        FB1[/central_data/timestamp]
        FB2[/central_data/batch_N_timestamp]
        FB3[JSON: timestamp, data array]
        FB4[JSON: timestamp, batch, data array]
    end
    
    %% Fluxo BLE
    subgraph "📡 Comunicação BLE"
        BLE1[Bike conecta via BLE]
        BLE2[Envia dados WiFi scan]
        BLE3[Central adiciona a pendingData]
        BLE4[Central envia NTP sync]
        BLE1 --> BLE2
        BLE2 --> BLE3
        BLE3 --> BLE4
    end
    
    %% Configurações
    subgraph "⚙️ Configurações"
        CFG1[/config.json - WiFi credentials]
        CFG2[/config.json - Firebase URL]
        CFG3[NTP server: pool.ntp.org]
        CFG4[Timezone: UTC-3]
        CFG5[Sync interval: 5min]
        CFG6[WiFi timeout: 30s]
        CFG7[Batch size: 8KB]
    end

    %% Conexões dos subgrafos
    G -.-> BLE2
    H -.-> BLE3
    S -.-> BLE4
    V -.-> FB1
    X -.-> FB2
    J -.-> CFG1
    V -.-> CFG2
    O -.-> CFG3

    %% Estilos
    classDef modeClass fill:#e1f5fe,stroke:#01579b,stroke-width:2px
    classDef processClass fill:#f3e5f5,stroke:#4a148c,stroke-width:2px
    classDef dataClass fill:#e8f5e8,stroke:#1b5e20,stroke-width:2px
    classDef errorClass fill:#ffebee,stroke:#b71c1c,stroke-width:2px
    
    class D,I,M modeClass
    class B,C,J,O,V,W,X processClass
    class S1,S2,S3,S4,S5,S6,FB1,FB2,FB3,FB4 dataClass
    class L,BB errorClass
```

## 📋 Resumo do Funcionamento

### 🔄 Ciclo Principal
1. **Modo BLE_ONLY** (padrão): Recebe dados das bicicletas via BLE
2. **Modo WIFI_SYNC** (temporário): Conecta WiFi e sincroniza com Firebase
3. **Modo SHUTDOWN**: Desliga WiFi e volta ao BLE

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