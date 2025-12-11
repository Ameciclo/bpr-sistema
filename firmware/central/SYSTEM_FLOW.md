# BPR Central System - Fluxo de Funcionamento

## 🎯 Visão Geral

Sistema central (ESP32C3 SuperMini) que atua como ponte entre bicicletas BLE e Firebase, com descoberta automática, configuração dinâmica e sincronização inteligente.

## 📊 Diagrama de Estados

```mermaid
flowchart TD
    %% Estados principais
    BOOT[🔄 BOOT<br/>main.cpp<br/>Inicialização]
    BLE_ONLY[🔵 BLE_ONLY<br/>state_machine.cpp<br/>Modo BLE Puro]
    WIFI_SYNC[📡 WIFI_SYNC<br/>firebase_manager.cpp<br/>Sincronização]
    SHUTDOWN[💤 SHUTDOWN<br/>state_machine.cpp<br/>Economia Energia]
    CONFIG_AP[⚙️ CONFIG_AP<br/>setup_server.cpp<br/>Configuração Inicial]

    %% Transições principais
    BOOT --> CONFIG_AP
    BOOT --> BLE_ONLY
    CONFIG_AP --> BLE_ONLY
    BLE_ONLY --> WIFI_SYNC
    WIFI_SYNC --> BLE_ONLY
    BLE_ONLY --> SHUTDOWN
    SHUTDOWN --> BLE_ONLY

    %% Condições das transições
    BOOT -.->|Config incompleta| CONFIG_AP
    BOOT -.->|Config OK| BLE_ONLY
    CONFIG_AP -.->|WiFi configurado| BLE_ONLY
    BLE_ONLY -.->|Timer sync OU<br/>Buffer cheio| WIFI_SYNC
    WIFI_SYNC -.->|Sync completa| BLE_ONLY
    BLE_ONLY -.->|Sem atividade<br/>por muito tempo| SHUTDOWN
    SHUTDOWN -.->|Timer wake-up OU<br/>atividade BLE| BLE_ONLY

    %% Estilos
    classDef bootState fill:#e1f5fe
    classDef bleState fill:#e8f5e8
    classDef wifiState fill:#fff3e0
    classDef configState fill:#fce4ec
    classDef sleepState fill:#f3e5f5

    class BOOT bootState
    class BLE_ONLY bleState
    class WIFI_SYNC wifiState
    class CONFIG_AP configState
    class SHUTDOWN sleepState
```

## 🗂️ Arquitetura de Arquivos

### 📁 Estrutura Modular
```
firmware/central/src/
├── main.cpp                  # 🚀 Ponto de entrada e loop principal
├── state_machine.cpp/.h      # 🔄 Máquina de estados do sistema
├── config_manager.cpp/.h     # ⚙️ Gerenciamento de configurações
├── config_loader.cpp/.h      # 📥 Carregamento de configs do Firebase
├── central_config.cpp/.h     # 🏢 Configurações específicas da central
├── ble_simple.cpp/.h         # 🔵 Servidor BLE simplificado
├── bike_manager.cpp/.h       # 🚲 Gerenciamento de bikes conectadas
├── bike_discovery.cpp/.h     # 🔍 Descoberta automática de bikes
├── firebase_manager.cpp/.h   # 🔥 Interface principal Firebase
├── firebase_client.h         # 🔗 Cliente HTTP Firebase
├── firebase_sync.h           # 🔄 Sincronização de dados
├── wifi_manager.cpp/.h       # 📶 Gerenciamento WiFi
├── ntp_manager.cpp/.h        # ⏰ Sincronização de horário
├── led_controller.cpp/.h     # 💡 Controle de LED com padrões
├── buffer_manager.cpp/.h     # 📦 Gerenciamento de buffer local
├── event_handler.cpp/.h      # 🎯 Tratamento de eventos
├── self_check.cpp/.h         # 🔧 Auto-diagnóstico do sistema
└── setup_server.cpp/.h       # 🌐 Servidor AP para configuração
```

## 🔄 Fluxo Detalhado por Estado

### 1️⃣ BOOT (main.cpp)
```mermaid
flowchart LR
    A[Power ON] --> B[main.cpp setup]
    B --> C[config_manager loadConfig]
    C --> D[self_check systemCheck]
    D --> E{Config válida?}
    E -->|Não| F[CONFIG_AP]
    E -->|Sim| G[led_controller bootPattern]
    G --> H[BLE_ONLY]
```

**Arquivos Envolvidos:**
- **main.cpp**: Inicialização geral e setup do hardware
- **config_manager.cpp**: Carrega configuração local ou padrão
- **self_check.cpp**: Verifica integridade do sistema
- **led_controller.cpp**: Indica status de boot

### 2️⃣ CONFIG_AP (setup_server.cpp)
```mermaid
flowchart LR
    A[Criar AP] --> B[setup_server startConfigServer]
    B --> C[Aguardar Config]
    C --> D[config_manager saveConfig]
    D --> E[Reiniciar]
    E --> F[BLE_ONLY]
```

**Arquivos Envolvidos:**
- **setup_server.cpp**: Servidor web para configuração inicial
- **config_manager.cpp**: Salva configurações recebidas
- **wifi_manager.cpp**: Gerencia ponto de acesso

### 3️⃣ BLE_ONLY (Modo Principal)
```mermaid
flowchart LR
    A[Iniciar BLE] --> B[ble_simple startServer]
    B --> C[bike_discovery scanForBikes]
    C --> D[bike_manager handleConnections]
    D --> E[buffer_manager storeData]
    E --> F{Trigger Sync?}
    F -->|Sim| G[WIFI_SYNC]
    F -->|Não| H[led_controller updateStatus]
    H --> C
```

**Arquivos Envolvidos:**
- **ble_simple.cpp**: Servidor BLE para comunicação com bikes
- **bike_discovery.cpp**: Descoberta automática de novas bikes
- **bike_manager.cpp**: Gerencia conexões e dados das bikes
- **buffer_manager.cpp**: Cache local de dados
- **led_controller.cpp**: Feedback visual do status
- **event_handler.cpp**: Processa eventos BLE

### 4️⃣ WIFI_SYNC (firebase_manager.cpp)
```mermaid
flowchart LR
    A[Conectar WiFi] --> B[wifi_manager connect]
    B --> C[ntp_manager syncTime]
    C --> D[config_loader updateConfig]
    D --> E[firebase_manager syncData]
    E --> F[buffer_manager clearSent]
    F --> G[Desconectar WiFi]
    G --> H[BLE_ONLY]
```

**Arquivos Envolvidos:**
- **wifi_manager.cpp**: Conexão e gerenciamento WiFi
- **ntp_manager.cpp**: Sincronização de horário via NTP
- **config_loader.cpp**: Download de configurações atualizadas
- **firebase_manager.cpp**: Sincronização principal com Firebase
- **firebase_client.h**: Cliente HTTP para Firebase
- **firebase_sync.h**: Lógica de sincronização
- **buffer_manager.cpp**: Gerencia dados a serem enviados

### 5️⃣ SHUTDOWN (state_machine.cpp)
```mermaid
flowchart LR
    A[Detectar Inatividade] --> B[state_machine enterShutdown]
    B --> C[Salvar Estado]
    C --> D[Desabilitar Periféricos]
    D --> E[Light Sleep]
    E --> F[Wake Timer]
    F --> G[BLE_ONLY]
```

**Arquivos Envolvidos:**
- **state_machine.cpp**: Controla transições e economia de energia
- **config_manager.cpp**: Salva estado atual
- **led_controller.cpp**: LED off durante shutdown

## 🔵 Sistema BLE (ble_simple.cpp)

### Fluxo de Comunicação
```mermaid
sequenceDiagram
    participant BD as bike_discovery.cpp
    participant BLE as ble_simple.cpp
    participant BM as bike_manager.cpp
    participant BUF as buffer_manager.cpp
    participant EH as event_handler.cpp
    
    BD->>BLE: Scan for "BPR_*" devices
    BLE->>BM: New bike discovered
    BM->>BLE: Accept connection
    BLE->>EH: Connection established
    EH->>BM: Handle bike data
    BM->>BUF: Store received data
    BUF->>BM: Confirm storage
    BM->>BLE: Send ACK to bike
```

### Características BLE
- **Service UUID**: Custom BPR service
- **Características**: Status, Config, Data Transfer
- **Descoberta**: Automática por prefixo "BPR_*"
- **Aprovação**: Via dashboard ou automática

## 🔥 Sistema Firebase (firebase_manager.cpp)

### Fluxo de Sincronização
```mermaid
sequenceDiagram
    participant SM as state_machine.cpp
    participant WM as wifi_manager.cpp
    participant NTP as ntp_manager.cpp
    participant CL as config_loader.cpp
    participant FM as firebase_manager.cpp
    participant BUF as buffer_manager.cpp
    
    SM->>WM: Connect WiFi
    WM->>NTP: Sync time
    NTP->>CL: Update configs
    CL->>FM: Start sync
    FM->>BUF: Get pending data
    BUF->>FM: Data batches
    FM->>FM: Upload to Firebase
    FM->>BUF: Mark as sent
    FM->>SM: Sync complete
```

### Endpoints Firebase
- **Configurações**: `/central_configs/{base_id}.json`
- **Dados das Bikes**: `/bikes/{bike_id}/sessions/{session_id}`
- **Heartbeat**: `/bases/{base_id}/last_heartbeat`
- **Status**: `/bases/{base_id}/status`

## 💡 Sistema LED (led_controller.cpp)

### Padrões de LED
```mermaid
graph TD
    A[led_controller.cpp] --> B[Padrão Boot 100ms rápido]
    A --> C[Padrão BLE 2s lento]
    A --> D[Padrão Sync 500ms médio]
    A --> E[Padrão Erro 50ms muito rápido]
    A --> F[Padrão Contagem N piscadas]
```

### Estados Visuais
- **Inicializando**: Piscar rápido (100ms) - `bootPattern()`
- **BLE Ativo**: Piscar lento (2s) - `bleReadyPattern()`
- **Bike Chegou**: 3 piscadas rápidas - `bikeArrivedPattern()`
- **Bike Saiu**: 1 piscada longa - `bikeLeftPattern()`
- **Contagem**: N piscadas = N bikes - `countPattern(n)`
- **Sincronizando**: Piscar médio (500ms) - `syncPattern()`
- **Erro**: Piscar muito rápido (50ms) - `errorPattern()`

## ⚙️ Sistema de Configuração

### Fluxo de Configuração
```mermaid
flowchart TD
    A[setup.sh] --> B[config.json básico]
    B --> C[main.cpp boot]
    C --> D[config_manager loadConfig]
    D --> E{Config completa?}
    E -->|Não| F[config_loader downloadFromFirebase]
    E -->|Sim| G[Usar config atual]
    F --> H[central_config updateConfig]
    H --> G
    G --> I[Sistema operacional]
```

### Hierarquia de Configuração
1. **config_manager.cpp**: Interface principal de configuração
2. **config_loader.cpp**: Download de configurações do Firebase
3. **central_config.cpp**: Configurações específicas da central
4. **setup_server.cpp**: Configuração inicial via web

## 🔧 Monitoramento e Diagnóstico

### Auto-Diagnóstico (self_check.cpp)
```mermaid
flowchart LR
    A[Startup] --> B[self_check systemCheck]
    B --> C[Verificar Memória]
    C --> D[Verificar WiFi]
    D --> E[Verificar BLE]
    E --> F[Verificar LED]
    F --> G{Tudo OK?}
    G -->|Sim| H[Continue Boot]
    G -->|Não| I[led_controller errorPattern]
```

### Heartbeat Automático
```mermaid
sequenceDiagram
    participant SM as state_machine.cpp
    participant BM as bike_manager.cpp
    participant FM as firebase_manager.cpp
    
    loop A cada sync_interval
        SM->>BM: Get bikes count
        BM->>SM: Connected bikes
        SM->>FM: Send heartbeat
        FM->>FM: Upload to /bases/{id}/last_heartbeat
    end
```

## 📊 Estrutura de Dados

### Configuração Central (central_config.cpp)
```cpp
struct CentralConfig {
    char base_id[32];
    uint16_t sync_interval_sec;
    uint16_t wifi_timeout_sec;
    uint8_t led_pin;
    uint16_t firebase_batch_size;
    char ntp_server[64];
    int32_t timezone_offset;
    LEDConfig led;
};
```

### Buffer de Dados (buffer_manager.cpp)
```cpp
struct DataBuffer {
    BikeData bikes[MAX_BIKES];
    uint16_t pending_count;
    uint32_t last_sync;
    bool sync_in_progress;
};
```

## 🚨 Tratamento de Erros

### Recuperação Automática (event_handler.cpp)
- **WiFi Fail**: Retry com timeout configurável
- **Firebase Fail**: Buffer local até próxima tentativa
- **BLE Fail**: Restart do servidor BLE
- **Config Fail**: Usar configurações padrão
- **Memory Full**: Sobrescrever dados mais antigos

### Logs e Debug
- **Serial Output**: Status detalhado a cada 30s
- **LED Patterns**: Feedback visual imediato
- **Firebase Logs**: Erros enviados para `/logs/{base_id}`

## 🔄 Integração com Ecosystem

### Comunicação com Bot Telegram
```mermaid
graph LR
    A[firebase_manager.cpp] --> B[Firebase Realtime DB]
    B --> C[Bot Telegram]
    C --> D[Notificações Usuários]
```

### Comunicação com Dashboard Web
```mermaid
graph LR
    A[firebase_manager.cpp] --> B[Firebase Realtime DB]
    B --> C[Web Dashboard]
    C --> D[Interface Visual]
```

### Fluxo Completo do Sistema
```mermaid
graph TB
    subgraph "🏢 Central (ESP32C3)"
        A[main.cpp] --> B[state_machine.cpp]
        B --> C[ble_simple.cpp]
        C --> D[bike_manager.cpp]
        D --> E[buffer_manager.cpp]
        E --> F[firebase_manager.cpp]
        F --> G[config_loader.cpp]
    end
    
    subgraph "🚲 Bicicletas"
        H[BLE Client] --> C
    end
    
    subgraph "🔥 Firebase"
        F --> I[Realtime Database]
        G --> I
    end
    
    subgraph "🤖 Bot + 🌐 Web"
        I --> J[Monitoramento]
        I --> K[Dashboard]
    end
```