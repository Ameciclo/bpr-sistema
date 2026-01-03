# BikePairing - Diagrama de Funcionamento

## 🎯 Visão Geral

O **BikePairing** é o orquestrador central que gerencia a comunicação BLE entre a central e as bicicletas, implementando um sistema de callbacks desacoplado e processamento sequencial de dados.

## 🏗️ Arquitetura do Sistema

```mermaid
graph TB
    subgraph "🔵 BikePairing (Orquestrador)"
        BP[BikePairing]
        SM[State Machine]
        Q[Data Queue]
        CB[Callbacks]
    end
    
    subgraph "📡 BLE Server"
        BLE[BLE Server]
        ADV[Advertising]
        CHAR[Characteristics]
    end
    
    subgraph "🚲 Módulos Especializados"
        BM[BikeManager]
        BUF[BufferManager]
        CFG[ConfigManager]
    end
    
    subgraph "🚴 Bicicletas"
        B1[Bike 1]
        B2[Bike 2]
        B3[Bike N]
    end
    
    BP --> BLE
    BP --> BM
    BP --> BUF
    BP --> CFG
    
    BLE <--> B1
    BLE <--> B2
    BLE <--> B3
    
    BLE --> CB
    CB --> SM
    SM --> Q
```

## 🔄 Máquina de Estados

```mermaid
stateDiagram-v2
    [*] --> PAIRING_IDLE
    
    PAIRING_IDLE --> PAIRING_RECEIVING_DATA : Bike envia dados
    PAIRING_IDLE --> PAIRING_SENDING_CONFIG : Config pendente
    
    PAIRING_RECEIVING_DATA --> PAIRING_IDLE : Dados processados
    PAIRING_RECEIVING_DATA --> PAIRING_IDLE : Timeout (30s)
    
    PAIRING_SENDING_CONFIG --> PAIRING_IDLE : Config enviada
    PAIRING_SENDING_CONFIG --> PAIRING_IDLE : Timeout (10s)
    
    PAIRING_IDLE --> [*] : exit()
```

## 📋 Fluxo de Inicialização

```mermaid
sequenceDiagram
    participant Main as Main Loop
    participant BP as BikePairing
    participant BLE as BLE Server
    participant BM as BikeManager
    
    Main->>BP: enter()
    BP->>BM: init()
    BP->>BLE: start()
    BP->>BLE: setDataCallback(onBikeDataReceived)
    BP->>BLE: setConnectCallback(onBikeConnected)
    BP->>BLE: setDisconnectCallback(onBikeDisconnected)
    BP->>BLE: setConfigCallback(onConfigRequest)
    BP->>BLE: setBusyStatus(false)
    BP-->>Main: ✅ Ready
```

## 🚲 Fluxo de Conexão de Bike

```mermaid
sequenceDiagram
    participant Bike as 🚲 Bicicleta
    participant BLE as BLE Server
    participant BP as BikePairing
    participant BM as BikeManager
    
    Bike->>BLE: Conecta via BLE
    BLE->>BP: onBikeConnected(bikeId)
    BP->>BP: triggerEvent(BIKE_ARRIVED)
    BP->>BM: canConnect(bikeId)?
    
    alt Bike Permitida
        BM-->>BP: ✅ true
        BP->>BM: hasConfigUpdate(bikeId)?
        alt Config Pendente
            BM-->>BP: ✅ true
            BP->>BM: getConfigForBike(bikeId)
            BM-->>BP: config JSON
            BP->>BLE: pushConfigToBike(bikeId, config)
            BP->>BM: markConfigSent(bikeId)
        end
    else Bike Bloqueada
        BM-->>BP: ❌ false
        BP->>BLE: forceDisconnectBike(bikeId)
    end
```

## 📊 Fluxo de Processamento de Dados

```mermaid
sequenceDiagram
    participant Bike as 🚲 Bicicleta
    participant BLE as BLE Server
    participant BP as BikePairing
    participant BM as BikeManager
    participant BUF as BufferManager
    
    Bike->>BLE: Envia dados JSON
    BLE->>BP: onBikeDataReceived(bikeId, jsonData)
    
    BP->>BLE: isCentralBusy()?
    alt Central Busy
        BLE-->>BP: ✅ true
        BP->>BLE: pushConfigToBike("busy response")
        BP-->>Bike: ⚠️ "Try again later"
    else Central Ready
        BLE-->>BP: ❌ false
        BP->>BM: canConnect(bikeId)?
        alt Bike Permitida
            BM-->>BP: ✅ true
            BP->>BP: Parse JSON
            BP->>BP: Check data type
            
            alt Heartbeat
                BP->>BM: updateHeartbeat(bikeId, battery, heap)
            else Data Upload
                BP->>BP: Sistema de Fila
                alt Fila Vazia
                    BP->>BP: processDataFromBike(bikeId, data)
                    BP->>BUF: addBikeData(bikeId, data)
                    BP->>BM: confirmDataUpload(bikeId)
                    BP->>BLE: pushConfigToBike(confirmation)
                else Fila Ocupada
                    BP->>BP: enqueueBike(bikeId, data)
                end
            end
        else Bike Bloqueada
            BM-->>BP: ❌ false
            BP-->>Bike: ❌ "Data rejected"
        end
    end
```

## 🔄 Sistema de Fila Sequencial

```mermaid
flowchart TD
    A[Bike envia dados] --> B{Fila vazia?}
    B -->|Sim| C[Processar imediatamente]
    B -->|Não| D{Mesma bike atual?}
    D -->|Sim| C
    D -->|Não| E[Adicionar à fila]
    
    C --> F[processDataFromBike]
    F --> G[BufferManager.addBikeData]
    G --> H[Enviar confirmação]
    H --> I[finishCurrentBike]
    I --> J{Fila tem dados?}
    J -->|Sim| K[Processar próxima]
    J -->|Não| L[Estado IDLE]
    
    E --> M[dataQueue.push]
    M --> N[Aguardar vez]
    
    K --> F
    
    subgraph "⏰ Timeout Control"
        T1[Timeout 30s por bike]
        T2[Timeout 10s para IDLE]
    end
    
    F -.-> T1
    I -.-> T2
```

## 🎭 Sistema de Callbacks

```mermaid
graph LR
    subgraph "📡 BLE Server Events"
        E1[onConnect]
        E2[onDisconnect]
        E3[onDataReceived]
        E4[onConfigRequest]
    end
    
    subgraph "🔄 Callback Registration"
        R1[setConnectCallback]
        R2[setDisconnectCallback]
        R3[setDataCallback]
        R4[setConfigCallback]
    end
    
    subgraph "🎯 BikePairing Handlers"
        H1[onBikeConnected]
        H2[onBikeDisconnected]
        H3[onBikeDataReceived]
        H4[onConfigRequest]
    end
    
    E1 --> R1 --> H1
    E2 --> R2 --> H2
    E3 --> R3 --> H3
    E4 --> R4 --> H4
```

## 📈 Loop Principal (update)

```mermaid
flowchart TD
    A["update() chamado"] --> B["BLE.updateAdvertisingStatus()"]
    B --> C["processDataQueue()"]
    C --> D{"Timeout bike atual?"}
    D -->|Sim| E["finishCurrentBike()"]
    D -->|Não| F{"Fila tem dados?"}
    F -->|Sim| G["Processar próxima bike"]
    F -->|Não| H["Event Timer Check"]
    
    E --> F
    G --> I["Parse JSON da fila"]
    I --> J["processDataFromBike()"]
    J --> H
    
    H --> K{"30s passaram?"}
    K -->|Sim| L["triggerEvent(BIKE_COUNT_CHANGED)"]
    K -->|Não| M["Fim do update"]
    L --> M
    
    Note right of L: "Main loop recebe evento<br/>e gerencia LED"
```

## 🚪 Fluxo de Saída (exit)

```mermaid
sequenceDiagram
    participant Main as Main Loop
    participant BP as BikePairing
    participant BLE as BLE Server
    
    Main->>BP: exit()
    BP->>BP: Limpar dataQueue
    BP->>BP: currentBike = ""
    BP->>BP: requestTimeout = 0
    BP->>BLE: setBusyStatus(true, 60s)
    BP->>BP: currentStatus = PAIRING_IDLE
    BP-->>Main: ✅ Exited (BLE still running)
    
    Note over BLE: BLE continua rodando<br/>mas marcado como BUSY<br/>para sync WiFi
```

## 🔧 Integração com Módulos

| **Módulo** | **Responsabilidade** | **Interface** |
|------------|---------------------|---------------|
| **BikeManager** | Estado das bikes, heartbeat, configs | `canConnect()`, `isAllowed()`, `updateHeartbeat()` |
| **BufferManager** | Armazenamento de dados | `addBikeData()` |
| **BLE Server** | Comunicação BLE | Callbacks registrados |
| **ConfigManager** | Configurações da central | `getConfig()` |

## ⚡ Características Principais

- **🔄 Desacoplado**: Sistema de callbacks elimina dependências diretas
- **📋 Sequencial**: Processa uma bike por vez evitando conflitos
- **⏰ Timeout**: Evita travamentos com timeouts automáticos
- **🎭 Event-Driven**: Sistema de eventos para LED e outros módulos
- **🛡️ Robusto**: Validações e tratamento de erros em cada etapa
- **🔧 Modular**: Delega responsabilidades para módulos especializados