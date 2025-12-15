# 🚲 BPR Bici - Fluxo do Sistema v2.0

## 🎯 Visão Geral

Sistema de bicicleta melhorado com máquina de estados clara, comunicação BLE otimizada e gerenciamento inteligente de energia.

## 📊 Diagrama de Estados

```mermaid
flowchart TD
    %% Estados principais
    BOOT[🔄 BOOT<br/>Inicialização<br/>Busca Base BLE]
    AT_BASE[🏠 AT_BASE<br/>Conectado à Base<br/>Sincronização]
    SCANNING[📡 SCANNING<br/>Coletando WiFi<br/>Procurando Base]
    LOW_POWER[⚡ LOW_POWER<br/>Economia de Energia<br/>Scans Reduzidos]
    DEEP_SLEEP[💤 DEEP_SLEEP<br/>Hibernação Profunda<br/>Wake-up Timer/Botão]

    %% Transições principais
    BOOT --> AT_BASE
    BOOT --> SCANNING
    AT_BASE --> SCANNING
    SCANNING --> AT_BASE
    SCANNING --> LOW_POWER
    LOW_POWER --> AT_BASE
    LOW_POWER --> SCANNING
    LOW_POWER --> DEEP_SLEEP
    DEEP_SLEEP --> BOOT

    %% Condições das transições
    BOOT -.->|Base BLE encontrada| AT_BASE
    BOOT -.->|Base não encontrada| SCANNING
    AT_BASE -.->|Conexão BLE perdida| SCANNING
    SCANNING -.->|Base BLE detectada| AT_BASE
    SCANNING -.->|Bateria baixa OU<br/>2h sem base| LOW_POWER
    LOW_POWER -.->|Base BLE detectada| AT_BASE
    LOW_POWER -.->|Bateria recuperada| SCANNING
    LOW_POWER -.->|Bateria crítica| DEEP_SLEEP
    DEEP_SLEEP -.->|Timer 1h OU<br/>botão pressionado| BOOT

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
    A[Power ON/Wake-up] --> B[Inicializar Hardware]
    B --> C[Carregar config.json]
    C --> D[Verificar Bateria]
    D --> E{Bateria OK?}
    E -->|Não| F[DEEP_SLEEP]
    E -->|Sim| G[Scan BLE por BPR*]
    G --> H{Base Encontrada?}
    H -->|Sim| I[AT_BASE]
    H -->|Não| J[SCANNING]
```

**Responsabilidades:**
- **main.cpp**: Orquestração geral e inicialização
- **config_manager.cpp**: Carrega configuração local
- **battery_monitor.cpp**: Verifica nível inicial
- **ble_client.cpp**: Scan por bases BLE

### 2️⃣ AT_BASE (at_base.cpp)
```mermaid
flowchart LR
    A[Conectar BLE] --> B[Enviar Status]
    B --> C[Receber Config]
    C --> D[Aplicar Config]
    D --> E[Enviar Dados WiFi]
    E --> F[Limpar Buffer]
    F --> G[Light Sleep 1s]
    G --> H{Ainda Conectado?}
    H -->|Sim| B
    H -->|Não| I[SCANNING]
```

**Responsabilidades:**
- **at_base.cpp**: Lógica de sincronização com base
- **ble_client.cpp**: Comunicação BLE (status, config, dados)
- **config_manager.cpp**: Atualiza configurações recebidas
- **wifi_scanner.cpp**: Fornece dados coletados
- **power_manager.cpp**: Light sleep entre operações

### 3️⃣ SCANNING (scanning.cpp)
```mermaid
flowchart LR
    A[Scan WiFi] --> B[Salvar Registros]
    B --> C[Delay 300ms]
    C --> D[Scan BLE]
    D --> E{Base Encontrada?}
    E -->|Sim| F[AT_BASE]
    E -->|Não| G{Bateria/Tempo OK?}
    G -->|Não| H[LOW_POWER]
    G -->|Sim| I[Sleep até próximo scan]
    I --> A
```

**Responsabilidades:**
- **scanning.cpp**: Coordena coleta de dados e busca por base
- **wifi_scanner.cpp**: Executa scans WiFi e gerencia buffer
- **power_manager.cpp**: Coordenação de rádio (WiFi → delay → BLE)
- **ble_client.cpp**: Procura bases disponíveis
- **battery_monitor.cpp**: Monitora condições para mudança de estado

### 4️⃣ LOW_POWER (low_power.cpp)
```mermaid
flowchart LR
    A[Entrar Modo Economia] --> B[Scan WiFi Reduzido]
    B --> C[Verificar Base]
    C --> D{Base/Bateria?}
    D -->|Base Encontrada| E[AT_BASE]
    D -->|Bateria Crítica| F[DEEP_SLEEP]
    D -->|Bateria Recuperada| G[SCANNING]
    D -->|Continuar| H[Sleep 1min]
    H --> B
```

**Responsabilidades:**
- **low_power.cpp**: Gerencia modo de economia
- **power_manager.cpp**: Reduz frequência CPU e potência WiFi
- **wifi_scanner.cpp**: Scans com frequência reduzida (15min)
- **ble_client.cpp**: Continua procurando base
- **battery_monitor.cpp**: Monitora recuperação ou criticidade

### 5️⃣ DEEP_SLEEP (deep_sleep.cpp)
```mermaid
flowchart LR
    A[Preparar Sleep] --> B[Salvar Buffer WiFi]
    B --> C[Salvar Config]
    C --> D[Desabilitar Periféricos]
    D --> E[Configurar Wake-up]
    E --> F[Entrar Deep Sleep]
    F --> G[Wake-up]
    G --> H[BOOT]
```

**Responsabilidades:**
- **deep_sleep.cpp**: Prepara e executa hibernação
- **wifi_scanner.cpp**: Salva buffer em LittleFS
- **config_manager.cpp**: Persiste configurações
- **power_manager.cpp**: Desabilita periféricos
- **ESP32**: Wake-up por timer (1h) ou botão

## 📡 Comunicação BLE

### Fluxo de Sincronização
```mermaid
sequenceDiagram
    participant Bici as 🚲 Bicicleta
    participant Base as 🏠 Base BLE
    
    Note over Bici,Base: Descoberta e Conexão
    Bici->>Base: Scan BLE por "BPR*"
    Base->>Bici: Advertise "BPR_Base_01"
    Bici->>Base: Connect BLE
    Base->>Bici: Connection Established
    
    Note over Bici,Base: Troca de Dados
    loop A cada 5 segundos
        Bici->>Base: Send Status (bateria, registros)
        Base->>Bici: Send Config (se atualizada)
        alt Buffer WiFi não vazio
            Bici->>Base: Send WiFi Data (JSON)
            Base->>Bici: ACK
            Bici->>Bici: Clear Buffer
        end
    end
    
    Note over Bici,Base: Desconexão
    alt Conexão perdida
        Base->>Bici: Disconnect
        Bici->>Bici: Estado → SCANNING
    end
```

### Estruturas de Dados BLE
```json
// Status da Bicicleta → Base
{
  "type": "bike_status",
  "bike_id": "bici_001",
  "battery_voltage": 3.82,
  "battery_percentage": 85,
  "records_count": 42,
  "timestamp": 1234567890,
  "heap": 174248
}

// Configuração Base → Bicicleta
{
  "bike_id": "bici_001",
  "base_ble_name": "BPR_Base_01",
  "version": 2,
  "scan_interval_sec": 300,
  "scan_interval_low_batt_sec": 900,
  "deep_sleep_sec": 3600,
  "min_battery_voltage": 3.45,
  "timestamp": 1234567890
}

// Dados WiFi Bicicleta → Base
{
  "scans": [
    {
      "ts": 1234567890,
      "bssid": "AA:BB:CC:DD:EE:FF",
      "rssi": -70,
      "ch": 6
    }
  ]
}
```

## ⚡ Coordenação de Rádio

### Problema ESP32-C3
- **WiFi e BLE compartilham o mesmo rádio**
- **Uso simultâneo pode causar interferência**
- **Perda de dados ou falhas de conexão**

### Solução Implementada
```mermaid
timeline
    title Coordenação WiFi/BLE
    
    section Scan WiFi
        Iniciar WiFi Scan : 5s timeout
        Processar Resultados : Salvar no buffer
        
    section Delay Coordenação
        Radio Delay : 300ms obrigatório
        
    section Scan BLE
        Iniciar BLE Scan : 5s timeout
        Processar Resultados : Conectar se base encontrada
        
    section Sleep
        Power Management : Sleep até próximo ciclo
```

### Benefícios
- ✅ **Evita conflitos de RF**
- ✅ **Mantém ambas funcionalidades ativas**
- ✅ **Melhora confiabilidade da comunicação**
- ✅ **Reduz consumo energético**

## 🔋 Gerenciamento de Energia

### Consumo por Estado
```mermaid
graph LR
    A[AT_BASE<br/>~5mA] --> B[SCANNING<br/>~50mA]
    B --> C[LOW_POWER<br/>~2mA]
    C --> D[DEEP_SLEEP<br/>~10µA]
    D --> A
    
    style A fill:#e8f5e8
    style B fill:#fff3e0
    style C fill:#fce4ec
    style D fill:#f3e5f5
```

### Otimizações Implementadas
- **CPU Frequency**: 80MHz (economia) / 160MHz (performance)
- **WiFi TX Power**: Reduzida em LOW_POWER (-1dBm vs 19.5dBm)
- **Sleep Modes**: Light sleep entre operações, deep sleep para hibernação
- **Peripheral Management**: Desabilita componentes desnecessários
- **Dynamic Scaling**: Ajuste automático baseado na bateria

## 🚨 Modo Emergência

### Ativação
```mermaid
flowchart LR
    A[Botão BOOT] --> B{Pressionado 3s?}
    B -->|Não| C[Continuar Normal]
    B -->|Sim| D[Modo Emergência]
    D --> E[LED Pisca Rápido]
    E --> F[Aguardar Comando]
    F --> G{Comando?}
    G -->|'r'| H[Reiniciar ESP32]
    G -->|'c'| I[Continuar Operação]
```

### Utilidade
- **Debug em campo**: Acesso via serial sem reflash
- **Recuperação**: Reinício forçado se sistema travado
- **Manutenção**: Pausa operação para diagnóstico

## 📊 Monitoramento e Debug

### Logs Estruturados
```
🚲 bici_001 | Estado: SCANNING | Uptime: 1234s
🔋 3.82V (85%) ✅ | 📡 42 registros
🔵 BLE: Desconectado | ⏱️ Estado há: 120s
```

### Indicadores LED
- **BOOT**: 3 piscadas rápidas (inicialização)
- **AT_BASE**: LED fixo (conectado)
- **SCANNING**: Piscada por scan (ativo)
- **LOW_POWER**: Piscada lenta (economia)
- **DEEP_SLEEP**: LED off (hibernando)
- **EMERGÊNCIA**: Piscadas muito rápidas (debug)

## 🔄 Integração com Sistema BPR

### Fluxo Completo
```mermaid
graph TB
    subgraph "🚲 Bicicleta (ESP32-C3)"
        A[WiFi Scanner] --> B[Buffer Local]
        B --> C[BLE Client]
        C --> D[Base Detection]
    end
    
    subgraph "🏠 Base/Hub (ESP32)"
        E[BLE Server] --> F[Bike Manager]
        F --> G[Firebase Sync]
    end
    
    subgraph "🔥 Firebase"
        H[Realtime Database]
        I[/bikes/{id}/sessions]
        J[/bikes/{id}/status]
    end
    
    subgraph "🤖 Bot Telegram"
        K[Monitor Sessões]
        L[Geolocalização]
        M[Notificações]
    end
    
    %% Conexões
    D --> E
    G --> H
    H --> I
    H --> J
    I --> K
    K --> L
    L --> M
```

### Vantagens da Nova Arquitetura
1. **Estados Claros**: Cada estado tem responsabilidade específica
2. **Configuração Dinâmica**: Base controla parâmetros remotamente
3. **Comunicação Confiável**: Coordenação de rádio evita interferências
4. **Energia Otimizada**: Modos adaptativos baseados na situação
5. **Código Modular**: Fácil manutenção e extensão
6. **Debug Avançado**: Logs estruturados e modo emergência
7. **Persistência Robusta**: Dados salvos antes de hibernação
8. **Recuperação Automática**: Tratamento de falhas e reconexão

## 🚀 Próximos Passos

- [ ] **Integração com Hub**: Implementar servidor BLE na base
- [ ] **Testes de Campo**: Validar autonomia e confiabilidade
- [ ] **Otimização**: Reduzir ainda mais o consumo energético
- [ ] **Segurança**: Implementar autenticação BLE
- [ ] **OTA Updates**: Atualização remota via BLE
- [ ] **Compressão**: Otimizar tamanho dos dados WiFi
- [ ] **Watchdog**: Recuperação automática de travamentos
- [ ] **Métricas**: Coleta de dados de performance