# BPR Sistema - Monorepo

Sistema completo de monitoramento de bicicletas com WiFi scanning, bot Telegram e interface web.

## 📁 Estrutura do Projeto

```
bpr-sistema/
├── firmware/           # Códigos para ESP32
│   ├── bici/          # Firmware da bicicleta v2.0 (WiFi scanner + BLE)
│   ├── central/       # Firmware da central v2.0 (ESP32-WROOM-32D)
│   └── common/        # Definições compartilhadas (BLE protocol)
├── bot/               # Bot do Telegram (@prarodarbot)
├── web/               # Dashboard web (Remix)
├── shared/            # Recursos compartilhados
├── docs/              # Documentação técnica
└── scripts/           # Scripts de configuração e deploy
```

## 🚀 Componentes

### 🚲 Firmware Bicicleta (ESP32)
- **Máquina de estados** bem definida (BOOT → CONFIG_REQUEST → SCANNING → AT_BASE → SLEEP)
- **Scanner WiFi automático** com intervalos configuráveis via Firebase
- **Cliente BLE** para comunicação com central
- **Gerenciamento de energia** com deep sleep inteligente
- **Monitor de bateria** com alertas automáticos
- **Buffer local persistente** no LittleFS
- **Configuração dinâmica** recebida da central via BLE
- **ID único automático** baseado no chip ID (bpr-XXXXXX)

#### 📁 Estrutura Firmware Bici
```
firmware/bici/src/
├── main.cpp              # 🚀 Máquina de estados principal
├── scanning.cpp          # 📡 Scanner WiFi com filtros RSSI
├── at_base.cpp           # 🔵 Cliente BLE para central
├── power_manager.cpp     # ⚡ Coordenação de rádios e sleep
├── config_manager.cpp    # ⚙️ Configurações dinâmicas
├── buffer_manager.cpp    # 📦 Buffer persistente LittleFS
├── lost.cpp              # 🔍 Estado de busca por central
└── utils.cpp             # 🛠️ Utilitários gerais
```

### 🏢 Firmware Central (ESP32-WROOM-32D)
- **Arquitetura modular** baseada em estados bem definidos
- **Servidor BLE** para comunicação com bikes autorizadas
- **Sistema de validação** rigoroso (allowed/pending/blocked)
- **Configuração dinâmica** por base e por bike via Firebase
- **Push automático** de configurações via BLE
- **LED inteligente** com padrões de status (Pin 2 built-in)
- **Heartbeat automático** para monitoramento
- **Self-check** de hardware no boot
- **Máquina de estados** (INITIAL_CONFIG_AP → INITIAL_SYNC → BIKE_PAIRING ↔ CLOUD_SYNC)
- **Timestamps precisos** adicionados pela central

#### 📁 Estrutura Firmware Central
```
firmware/central/src/
├── main.cpp                    # 🚀 Entry point + self-check
├── config_manager.cpp          # ⚙️ Configurações da central
├── config_ap.cpp               # 📱 Estado: Configuração via AP
├── ble_server.cpp              # 🔵 Estado: Servidor BLE + filtros
├── cloud_sync.cpp              # 📡 Estado: Sincronização completa
├── buffer_manager.cpp          # 📦 Buffer local de dados
├── led_controller.cpp          # 💡 Padrões de LED inteligentes
├── bike_manager.cpp            # 🚲 Registro e validação de bikes
├── bike_pairing.cpp            # 🔗 Pareamento e comunicação BLE
├── config_credentials.cpp      # 🔑 Gerenciamento de credenciais
├── sync_monitor.cpp            # 📊 Monitor de sincronização
├── time_sync.cpp               # ⏰ Sincronização de tempo NTP
├── upload_queue.cpp            # 📤 Fila de upload para Firebase
└── self_check.cpp              # 🔧 Diagnóstico de hardware
```

#### 🚨 Sistema de LED (Pin 2 Built-in)

| Padrão | Intervalo | Significado |
|--------|-----------|-------------|
| **Boot** | 100ms | Inicializando sistema |
| **Config AP** | 200ms | Modo configuração ativo |
| **BLE Ready** | 2000ms | Aguardando bikes |
| **Bike Arrived** | 3x 150ms | Nova bike conectou |
| **Bike Left** | 1x 800ms | Bike desconectou |
| **Count** | N piscadas | N bikes conectadas (a cada 30s) |
| **Sync** | 500ms | Sincronizando com Firebase |
| **Error** | 50ms | Erro crítico |

#### ⚙️ Sistema de Configuração da Central

#### ⚙️ Sistema de Configuração da Central

##### **Setup Inicial:**
```bash
cd firmware/central

# 1. Configurar credenciais WiFi e Firebase
./setup.sh

# 2. Upload filesystem (configs)
pio run --target uploadfs

# 3. Upload firmware
pio run --target upload

# 4. Monitor serial
pio device monitor
```

##### **Primeira Execução:**
1. **Central inicia** → Modo CONFIG_AP (config inválida)
2. **Conectar WiFi**: `BPR_Hub_Config` (senha: `botaprarodar`)
3. **Acessar**: http://192.168.4.1
4. **Configurar**: WiFi, Firebase URL, API Key, Base ID
5. **Salvar** → Central reinicia → Primeira sync obrigatória
6. **Sync sucesso** → Modo BLE_ONLY ativo

##### **Funcionamento Normal:**
```
BIKE_PAIRING (300s) → CLOUD_SYNC (30s) → BIKE_PAIRING (300s) → ...
```

##### **Configurações Disponíveis:**
- **base_id** - Identificador único da central
- **sync_interval_sec** - Intervalo de sincronização (padrão: 300s)
- **wifi_timeout_sec** - Timeout de conexão WiFi (padrão: 30s)
- **led_pin** - Pino do LED de status (padrão: 2)
- **firebase_batch_size** - Tamanho máximo do batch (padrão: 8000 bytes)
- **ntp_server** - Servidor NTP (padrão: pool.ntp.org)
- **timezone_offset** - Fuso horário em segundos (padrão: -10800 = GMT-3)
- **led.*** - Configurações específicas de cada padrão de LED
- **limits.max_bikes** - Máximo de bikes por central
- **fallback.*** - Configurações de recuperação de erro

##### **Estrutura no Firebase:**
```
/bases/{base_id}/
├── configs/         # Configurações da central
├── bikes/          # Registro de bikes (allowed/pending/blocked)
└── last_heartbeat/ # Status da central

/bike_configs/{bike_id}/  # Configurações por bike
```

##### **Upload das Configurações:**
```bash
cd scripts
cp central_configs.json.example central_configs.json
# Editar senhas WiFi
node upload_central_configs.js  # Sobe configs para Firebase
```

##### **Sistema de Gerenciamento de Bikes:**

**Estados das Bikes:**
- **`allowed`**: Pode conectar e enviar dados
- **`pending`**: Pode conectar, dados ignorados (só registra visitas)
- **`blocked`**: Não consegue conectar

**Heartbeat Automático:**
Cada central envia heartbeat para `/bases/{base_id}/last_heartbeat` contendo:
- **timestamp** - Quando foi enviado (com timestamp_human)
- **bikes_connected** - Quantas bikes estão conectadas
- **heap** - Memória livre (para debug)
- **uptime** - Tempo ligada em segundos

##### **Vantagens da Arquitetura v2.0:**
- ✅ **Modular**: Cada arquivo tem responsabilidade única
- ✅ **Configurável**: Zero hardcoding, tudo via Firebase
- ✅ **Inteligente**: Push automático de configs
- ✅ **Seguro**: Validação rigorosa de bikes
- ✅ **Robusto**: Self-check e recuperação de erros
- ✅ **Eficiente**: Buffer local e sync otimizada
- ✅ **Escalável**: Fácil adicionar novas funcionalidades
- ✅ **Observável**: Logs estruturados e métricas

### 🤖 Bot Telegram (@prarodarbot)
- **Monitoramento em tempo real** de bicicletas via Firebase
- **Notificações automáticas** de chegada/saída da base
- **Geolocalização WiFi** usando Google Geolocation API
- **Comandos interativos** para consulta de status e rotas
- **Cálculo de distâncias** percorridas baseado em pontos WiFi
- **Sistema de assinaturas** para notificações personalizadas
- **Canal público** para acompanhamento das atividades
- **Interface conversacional** para usuários e administradores

#### 🎯 Funcionalidades Principais
- ✅ **Chegada na base**: Notifica quando bike se conecta ao WiFi da base
- 🚀 **Saída da base**: Notifica quando bike inicia nova sessão de coleta
- 📡 **Scans WiFi**: Mostra redes detectadas durante o percurso em tempo real
- 📍 **Geolocalização**: Converte dados WiFi em coordenadas usando Google API
- 📏 **Cálculo de rota**: Calcula distância percorrida baseada nos pontos coletados
- 🔋 **Status de bateria**: Monitora nível de bateria das bicicletas
- 📊 **Estatísticas**: Resumos de sessões, scans coletados e conexões
- 🏢 **Monitor de estações**: Verifica heartbeat e status das centrais
- 📱 **Assinaturas**: Sistema de notificações personalizadas por usuário
- 📺 **Canal público**: Publicação automática de atividades

#### 🤖 Comandos Disponíveis
- `/start` - Mensagem de boas-vindas e instruções
- `/status [bike]` - Status atual de uma bicicleta específica
- `/rota [bike]` - Última rota calculada com distância percorrida
- `/bikes` - Lista todas as bicicletas monitoradas
- `/estacao [id]` - Status de uma estação específica
- `/seguir [bike/estacao/sistema]` - Receber notificações personalizadas
- `/parar [bike/estacao/sistema]` - Parar notificações
- `/minhas` - Ver suas assinaturas ativas
- `/help` - Ajuda completa com todos os comandos
- `/ping` - Teste de funcionamento do bot

#### 📁 Estrutura do Código Bot
```
bot/src/
├── index.js                    # 🚀 Ponto de entrada e comandos principais
├── config/
│   └── firebase.js            # 🔥 Configuração e listeners Firebase
├── services/
│   ├── bikeMonitor.js         # 🚲 Monitor principal de bicicletas
│   ├── stationMonitor.js      # 🏢 Monitor de estações/centrais
│   ├── geolocation.js         # 📍 Conversão WiFi → Coordenadas
│   ├── rideCalculator.js      # 📏 Cálculo de rotas e distâncias
│   ├── channelPublisher.js    # 📺 Publicação no canal público
│   └── subscriptionManager.js # 📱 Gerenciamento de assinaturas
└── utils/
    └── dataConverter.js       # 🔄 Conversão entre formatos de dados
```

#### 🔧 Principais Serviços

**🚲 BikeMonitor** (`bikeMonitor.js`)
- Escuta novas sessões e scans WiFi em tempo real
- Processa geolocalização via Google API
- Calcula rotas e distâncias percorridas
- Monitora bateria e envia alertas
- Gerencia notificações para usuários assinantes

**🏢 StationMonitor** (`stationMonitor.js`)
- Verifica heartbeat das centrais (timeout 30min)
- Monitora bikes disponíveis por estação
- Detecta estações online/offline
- Fornece status consolidado das estações

**📍 Geolocation** (`geolocation.js`)
- Converte dados WiFi para coordenadas via Google API
- Calcula distâncias usando fórmula de Haversine
- Processa rotas completas baseadas em múltiplos scans
- Filtra pontos inválidos e otimiza precisão

**📏 RideCalculator** (`rideCalculator.js`)
- Gerencia viagens ativas em cache local
- Calcula CO₂ economizado (0.145kg/km)
- Atualiza métricas das bikes e estatísticas públicas
- Filtra viagens muito curtas (< 80m)
- Salva histórico completo no Firebase

**📺 ChannelPublisher** (`channelPublisher.js`)
- Publica atividades no canal público @prarodar_updates
- Controla frequência de publicações (throttling)
- Formata mensagens para diferentes tipos de eventos
- Gera links para visualização de rotas

**📱 SubscriptionManager** (`subscriptionManager.js`)
- Gerencia assinaturas por usuário (bikes, estações, sistema)
- Cache local sincronizado com Firebase
- Notificações personalizadas por tipo de evento
- Sistema de opt-in/opt-out flexível

#### 🔧 Tecnologias Utilizadas
- **Telegraf.js** - Framework para bots Telegram
- **Firebase Admin SDK** - Conexão com Realtime Database
- **Google Geolocation API** - Conversão WiFi → Coordenadas
- **Axios** - Requisições HTTP para APIs externas
- **Node.js** - Runtime JavaScript

#### 📊 Estrutura de Dados Monitorada
```javascript
// Firebase: /bikes/{bikeId}/sessions/{sessionId}
{
  "start": 1733459200,
  "end": null,
  "mode": "normal",
  "scans": [
    [timestamp, [[ssid, bssid, rssi, channel]]]
  ],
  "battery": [[time, level, charging]],
  "connections": [[time, event, base, ip]]
}

// Firebase: /subscriptions/{userId}
{
  "bikes": ["intenso", "rapida"],
  "stations": ["base01"],
  "system": false
}

// Firebase: /rides/{bikeId}/{rideId}
{
  "start_ts": 1733459200,
  "end_ts": 1733460500,
  "km": 2.8,
  "co2_saved_g": 406,
  "route": [{"lat": -8.064, "lng": -34.882}],
  "points_count": 12,
  "duration_min": 22
}
```

### 🌐 Dashboard Web (Remix)
- **Framework**: Remix com Vite
- **Status**: 🚧 Em desenvolvimento inicial
- **Estrutura básica**: Componente raiz implementado
- **Integração Firebase**: Planejada
- **Próximos passos**: Dashboard principal, mapas, relatórios

#### 📁 Estrutura Web
```
web/
├── app/
│   └── root.jsx          # 🚀 Componente raiz básico
├── package.json          # 📦 Dependências Remix
├── vite.config.js        # ⚙️ Configuração Vite
└── README.md            # 📚 Documentação específica
```

## 🛠️ Desenvolvimento

### Pré-requisitos
- **PlatformIO** (para firmware ESP32)
- **Node.js 18+** (para bot e web)
- **Firebase CLI** (para deploy de functions)
- **Google Cloud Account** (para Geolocation API)
- **ESP32-WROOM-32D** (para central)
- **ESP32** genérico (para bicicletas)

### Setup Inicial
```bash
# Clone o repositório
git clone <repo-url>
cd bpr-sistema

# Setup workspace (monorepo)
npm install

# Setup firmware bicicleta
cd firmware/bici
pio run --target upload
pio run --target uploadfs

# Setup firmware central
cd ../central
./setup.sh  # Configurar WiFi e Firebase
pio run --target uploadfs
pio run --target upload

# Setup bot
cd ../../bot
npm install
cp .env.example .env  # Configurar variáveis
node scripts/test/check-env.js  # Testar configuração

# Setup web
cd ../web
npm install
npm run dev  # Estrutura básica disponível
```

### 🤖 Configuração do Bot

#### Variáveis de Ambiente Obrigatórias
```bash
# Telegram
TELEGRAM_BOT_TOKEN=seu_token_do_botfather
ADMIN_CHAT_ID=seu_chat_id_para_notificacoes

# Firebase
FIREBASE_PROJECT_ID=seu_projeto_firebase
FIREBASE_DATABASE_URL=https://projeto.firebaseio.com
FIREBASE_PRIVATE_KEY="-----BEGIN PRIVATE KEY-----\n..."
FIREBASE_CLIENT_EMAIL=service-account@projeto.iam.gserviceaccount.com

# Google Geolocation API
GOOGLE_GEOLOCATION_API_KEY=sua_chave_da_api_google
```

#### Scripts Úteis
```bash
# Desenvolvimento
npm run dev                    # Nodemon com hot reload
npm start                      # Produção

# Testes
node scripts/test/check-env.js      # Verificar configuração
node scripts/test/test-geolocation.js  # Testar API Google
node scripts/test/test-message.js      # Testar envio de mensagens

# Webhook (para produção)
node scripts/webhook/set-webhook.js    # Configurar webhook
node scripts/webhook/check-webhook.js  # Verificar webhook
```

## 📦 Deploy

Cada componente tem seu próprio processo de deploy:

### 🚲 Firmware Bicicleta
```bash
cd firmware/bici
pio run --target uploadfs     # Upload config.json
pio run --target upload       # Upload código
pio device monitor            # Monitor serial
```

### 🏢 Firmware Central
```bash
cd firmware/central
./setup.sh                    # Configurar WiFi, Firebase e Base ID
pio run --target uploadfs     # Upload configurações
pio run --target upload       # Upload código
pio device monitor            # Monitor serial
```

### 🤖 Bot Telegram
```bash
cd bot

# Opção 1: Servidor próprio
npm install --production
PM2_HOME=/path pm2 start src/index.js --name prarodarbot

# Opção 2: Firebase Functions
cd functions
npm run deploy

# Opção 3: Docker
docker build -t prarodarbot .
docker run -d --env-file .env prarodarbot
```

### 🌐 Web
```bash
cd web
npm run build
# Deploy: Vercel/Netlify (configurar manualmente)
```

## 🔗 Integrações

Todos os componentes se integram via:

### 🔥 Firebase Realtime Database
- **Central** → Escreve dados de bikes com timestamp preciso
- **Bicicletas** → Enviam dados via BLE para central
- **Bot** → Escuta mudanças em tempo real e processa geolocalização
- **Web** → Dashboard com dados consolidados
- **Configurações** → Centralizadas por base e por bike
- **Validação** → Sistema rigoroso de allowed/pending/blocked

### 🌐 APIs Externas
- **Google Geolocation API** → Conversão WiFi para coordenadas
- **Telegram Bot API** → Notificações e comandos
- **NTP Servers** → Sincronização de horário

### 📡 Fluxo de Dados Atualizado
```mermaid
flowchart TD
    subgraph "🚲 Bicicleta (ESP32)"
        A1[WiFi Scanner] --> A2[Buffer Local LittleFS]
        A2 --> A3[Detecta Central BLE]
        A3 --> A4[Envia via BLE]
    end
    
    subgraph "🏢 Central (ESP32-WROOM-32D)"
        A4 --> B1[Valida Bike]
        B1 --> B2{Status?}
        B2 -->|allowed| B3[Processa Dados]
        B2 -->|pending| B4[Registra Visita]
        B2 -->|blocked| B5[Rejeita]
        B3 --> B6[Adiciona Timestamp]
        B6 --> B7[Buffer Local]
        B7 --> B8[Sync Firebase]
    end
    
    subgraph "🔥 Firebase"
        B8 --> C1[Realtime Database]
        C1 --> C2[/bases/{id}/bikes]
        C1 --> C3[/bike_configs/{id}]
        C1 --> C4[/bikes/{id}/sessions]
    end
    
    subgraph "🤖 Bot Telegram"
        C4 --> D1[Monitor Sessões]
        D1 --> D2[Processar Scans]
        D2 --> D3[Google Geolocation]
        D3 --> D4[Calcular Viagens]
        D4 --> D5[Notificar Usuários]
        D5 --> D6[Canal Público]
    end
    
    subgraph "🌐 Dashboard Web"
        C1 --> E1[Interface Remix]
        E1 --> E2[Mapas e Métricas]
    end
```

## 🗄️ Estrutura de Dados (Firebase)

### 🤖 Dados Específicos do Bot

O bot monitora e processa os seguintes dados em tempo real:

#### `/bikes/{bikeId}/sessions/{sessionId}` - Sessões Ativas
```json
{
  "bike_id": "bpr-a1b2c3",
  "session_start_millis": 45000,
  "scans": [
    [47000, [["NET_5G", "AA:BB:CC:11:22:33", -70, 6]]],
    [52000, [["CLARO_WIFI", "CC:DD:EE:44:55:66", -82, 11]]]
  ],
  "battery": [[47000, 85], [52000, 84]],
  "hub_receive_timestamp": 1733459800,
  "hub_receive_timestamp_human": "2024-12-06 10:30:00 UTC-3"
}
```

#### Processamento pelo Bot
1. **Monitor de Sessões**: Escuta mudanças em `/bikes/{id}/sessions`
2. **Monitor de Scans**: Processa novos scans WiFi em tempo real
3. **Geolocalização**: Converte arrays WiFi para coordenadas via Google API
4. **Cálculo de Viagens**: Distância, CO₂ economizado, duração
5. **Sistema de Assinaturas**: Notificações personalizadas por usuário
6. **Canal Público**: Publicações automáticas para @prarodar_updates
7. **Comandos**: Responde consultas com dados processados e formatados

## 🗄️ Estrutura de Dados (Firebase)

O Firebase Realtime Database é estruturado como uma árvore JSON otimizada para:
- Leituras rápidas
- Escritas baratas
- Acesso simultâneo (central, bicicletas, dashboard, bot)
- Caching local nos ESP32

### Principais Coleções:

#### `/bases/{base_id}/configs` - Configurações da Central
```json
{
  "base_id": "base01",
  "sync_interval_sec": 300,
  "wifi_timeout_sec": 30,
  "led_pin": 2,
  "firebase_batch_size": 8000,
  "ntp_server": "pool.ntp.org",
  "timezone_offset": -10800,
  "led": {
    "boot_ms": 100,
    "ble_ready_ms": 2000,
    "wifi_sync_ms": 500
  },
  "limits": {
    "max_bikes": 10,
    "batch_size": 8000
  }
}
```

#### `/bike_configs/{bike_id}` - Configurações por Bike
```json
{
  "version": 2,
  "bike_name": "Bike Centro 01",
  "dev_mode": false,
  "wifi": {
    "scan_interval_sec": 300,
    "scan_timeout_ms": 5000,
    "max_networks": 20,
    "rssi_threshold": -90
  },
  "ble": {
    "base_name": "BPR Central",
    "scan_time_sec": 5
  },
  "power": {
    "deep_sleep_duration_sec": 3600,
    "radio_coordination_delay_ms": 300
  },
  "battery": {
    "critical_voltage": 3.2,
    "low_voltage": 3.45
  }
}
```

#### `/bases/{base_id}/bikes` - Registro de Bikes
```json
{
  "bpr-a1b2c3": {
    "status": "allowed",
    "first_seen": 1733459200,
    "last_heartbeat": {
      "timestamp": 1733459800,
      "timestamp_human": "2024-12-06 10:30:00 UTC-3",
      "battery": 85,
      "heap": 45000
    }
  },
  "bpr-x7y9z1": {
    "status": "pending",
    "first_seen": 1733459300,
    "last_visit": 1733459800,
    "visit_count": 3
  }
}
```

#### `/bikes/{bike_id}` - Dados das Bicicletas
```json
{
  "base_id": "base01",
  "uid": "bpr-a1b2c3",
  "battery_voltage": 3.82,
  "status": "active",
  "last_ble_contact": 1733459190,
  "last_position": {
    "lat": -8.064,
    "lng": -34.882,
    "source": "wifi",
    "accuracy": 50
  },
  "metrics": {
    "km_total": 213.4,
    "rides_total": 58,
    "co2_saved_total_g": 30930
  },
  "sessions": {
    "session_1733459200": {
      "bike_id": "bpr-a1b2c3",
      "session_start_millis": 45000,
      "scans": [...],
      "battery": [...],
      "hub_receive_timestamp": 1733459800
    }
  }
}
```

#### `/wifi_scans` - Scans WiFi
```json
{
  "wifi_scans": {
    "bike07": {
      "1733459205": [
        { "ssid": "NET_5G", "bssid": "AA:BB:CC:11:22:33", "rssi": -70 },
        { "ssid": "CLARO_WIFI", "bssid": "CC:DD:EE:44:55:66", "rssi": -82 }
      ]
    }
  }
}
```

#### `/rides` - Histórico de Viagens
```json
{
  "rides": {
    "bike07": {
      "ride_001": {
        "start_ts": 1733458000,
        "end_ts": 1733459300,
        "km": 2.8,
        "co2_saved_g": 410,
        "route": [
          { "lat": -8.064, "lng": -34.882 },
          { "lat": -8.061, "lng": -34.880 }
        ]
      }
    }
  }
}
```

#### `/alerts` - Alertas do Sistema
```json
{
  "alerts": {
    "battery_low": {
      "bike07": 1733459301
    },
    "left_base": {
      "bike07": 1733459301
    }
  }
}
```

#### `/public_stats` - Estatísticas Públicas
```json
{
  "public_stats": {
    "total_rides_month": 142,
    "km_month": 281.3,
    "co2_saved_month_g": 40350,
    "bikes_active": 9
  }
}
```

#### `/adopters` - Adoção de Bicicletas
```json
{
  "adopters": {
    "bike07": {
      "user_id": "telegram_123456",
      "notify_on": ["ride_end", "battery_low", "monthly_report"]
    }
  }
}
```

## 📚 Documentação

### 📁 Estrutura Completa
```
bpr-sistema/
├── 🚲 firmware/           # Códigos ESP32
│   ├── bici/              # Firmware bicicleta v2.0 (máquina de estados)
│   ├── central/           # Firmware central v2.0 (ESP32-WROOM-32D)
│   └── common/            # Definições compartilhadas (BLE protocol)
├── 🤖 bot/                # Bot Telegram (@prarodarbot)
│   ├── src/               # Código principal organizado
│   ├── functions/         # Firebase Functions (deploy)
│   ├── scripts/           # Utilitários e testes
│   ├── tools/             # Ferramentas auxiliares
│   └── docs/              # Documentação específica
├── 🌐 web/                # Dashboard web (Remix + Vite)
├── 📊 shared/             # Recursos compartilhados
├── 📚 docs/               # Documentação técnica
└── 🔧 scripts/            # Scripts de configuração e deploy
```

### 🔄 Fluxo Completo do Sistema

```mermaid
flowchart TD
    subgraph "🚲 Bicicleta"
        A1[Inicialização] --> A2[Carregar Config]
        A2 --> A3{Botão FLASH?}
        A3 -->|Sim| A4[Modo Config AP]
        A3 -->|Não| A5{Base Detectada?}
        A5 -->|Sim| A4
        A5 -->|Não| A6[Modo Scanner]
        
        A6 --> A7[Scan WiFi]
        A7 --> A8[Armazenar Local]
        A8 --> A9{Na Base?}
        A9 -->|Não| A10[Delay 5s]
        A9 -->|Sim| A11[Conectar Base]
        A10 --> A7
        A11 --> A12[Sync NTP]
        A12 --> A13[Upload Firebase]
        A13 --> A14[Limpar Dados]
        A14 --> A15[Delay 30s]
        A15 --> A7
    end
    
    subgraph "🔥 Firebase"
        B1[(Realtime DB)]
        B2[/bikes/{id}/sessions]
        B3[/bikes/{id}/status]
        B4[/central_configs]
        B5[/config]
    end
    
    subgraph "🤖 Bot Telegram"
        C1[Inicialização] --> C2[Conectar Firebase]
        C2 --> C3[Listen Sessions]
        C3 --> C4{Nova Sessão?}
        C4 -->|Sim| C5[Notificar Início]
        C4 -->|Não| C6[Listen Scans]
        C6 --> C7{Novo Scan?}
        C7 -->|Sim| C8[Processar Redes]
        C8 --> C9[Google Geolocation]
        C9 --> C10[Calcular Posição]
        C10 --> C11[Notificar Localização]
        
        C12[Comandos Telegram] --> C13{Tipo Comando}
        C13 -->|/status| C14[Buscar Status]
        C13 -->|/rota| C15[Calcular Rota]
        C13 -->|/bikes| C16[Listar Bikes]
        C14 --> C17[Responder]
        C15 --> C17
        C16 --> C17
    end
    
    subgraph "🏢 Central/Base"
        D1[ESP32C3 SuperMini] --> D2[Ponto Acesso WiFi]
        D2 --> D3[Coleta Dados Bikes]
        D3 --> D4[LED Status]
        D4 --> D5[Heartbeat Firebase]
        D5 --> D6[Config Dinâmica]
    end
    
    subgraph "👥 Usuários"
        E1[Admin Chat] 
        E2[Comandos Bot]
        E3[Dashboard Web]
    end
    
    %% Conexões
    A13 --> B1
    B1 --> C3
    B1 --> C6
    C5 --> E1
    C11 --> E1
    C17 --> E2
    D5 --> B1
    B1 --> E3
```

### 🤖 Fluxo Detalhado do Bot Telegram

```mermaid
sequenceDiagram
    participant B as 🚲 Bicicleta
    participant F as 🔥 Firebase
    participant Bot as 🤖 Bot Telegram
    participant G as 🌍 Google API
    participant U as 👤 Usuário
    participant A as 👨‍💼 Admin
    
    Note over B,A: 🚀 Início de Nova Sessão
    B->>F: Upload nova sessão
    F->>Bot: Trigger: child_changed
    Bot->>Bot: Detectar nova sessão
    Bot->>A: 📱 "🚴 Bike INTENSO - NOVA SESSÃO INICIADA"
    
    Note over B,A: 📡 Coleta de Dados WiFi
    loop Cada Scan WiFi
        B->>B: Scan redes WiFi
        B->>F: Upload scan data
        F->>Bot: Trigger: child_added (scans)
        Bot->>Bot: Processar redes WiFi
        Bot->>G: Converter WiFi → Coordenadas
        G->>Bot: Latitude, Longitude, Accuracy
        Bot->>A: 📱 "📡 5 redes detectadas\n📍 Lat: -8.064, Lng: -34.882"
    end
    
    Note over B,A: 💬 Comandos de Usuário
    U->>Bot: /status intenso
    Bot->>F: Buscar dados da bike
    F->>Bot: Sessão atual + status
    Bot->>Bot: Formatar resumo
    Bot->>U: 📱 "🚴 Resumo - Bike INTENSO\n📍 Status: 🚀 Ativa"
    
    U->>Bot: /rota intenso
    Bot->>F: Buscar scans da sessão
    F->>Bot: Array de scans
    loop Para cada scan
        Bot->>G: Converter WiFi → Coordenadas
        G->>Bot: Posição calculada
    end
    Bot->>Bot: Calcular distância total
    Bot->>U: 📱 "🗺️ Distância: 2.8 km\n📍 12 pontos coletados"
    
    Note over B,A: 🔋 Monitoramento Contínuo
    B->>F: Update bateria/conexões
    F->>Bot: Trigger: mudança de status
    alt Bateria baixa
        Bot->>A: 📱 "🔋 Bateria crítica: 15%"
    else Desconexão prolongada
        Bot->>A: 📱 "⚠️ Bike offline há 2h"
    end
```

### 🔄 Estados e Transições do Bot

```mermaid
stateDiagram-v2
    [*] --> Inicializando
    Inicializando --> Conectando_Firebase
    Conectando_Firebase --> Aguardando_Eventos
    
    Aguardando_Eventos --> Processando_Nova_Sessao : Nova sessão detectada
    Aguardando_Eventos --> Processando_Scan : Novo scan WiFi
    Aguardando_Eventos --> Processando_Comando : Comando recebido
    
    Processando_Nova_Sessao --> Notificando_Admin : Formatar mensagem
    Processando_Scan --> Consultando_Google : Converter WiFi
    Processando_Comando --> Consultando_Firebase : Buscar dados
    
    Consultando_Google --> Notificando_Admin : Posição calculada
    Consultando_Firebase --> Respondendo_Usuario : Dados formatados
    
    Notificando_Admin --> Aguardando_Eventos
    Respondendo_Usuario --> Aguardando_Eventos
    
    Aguardando_Eventos --> Erro : Falha de conexão
    Erro --> Reconectando : Retry automático
    Reconectando --> Aguardando_Eventos : Sucesso
    Reconectando --> [*] : Falha crítica
```

### 🎯 Casos de Uso do Bot

#### 📱 Para Administradores
- **Monitoramento em tempo real**: Recebe notificações automáticas de todas as atividades
- **Alertas críticos**: Bateria baixa, desconexões prolongadas, falhas de sistema
- **Visão geral**: Status consolidado de todas as bicicletas
- **Debugging**: Logs detalhados de scans WiFi e geolocalização

#### 👥 Para Usuários Finais
- **Consulta de status**: Ver se bike está disponível e onde está
- **Histórico de rotas**: Visualizar trajetos percorridos
- **Estatísticas pessoais**: Distância total, número de viagens
- **Notificações**: Alertas personalizados (opcional)

#### 🔧 Para Desenvolvedores
- **API via comandos**: Acesso programático aos dados
- **Testes automatizados**: Scripts de verificação de funcionamento
- **Monitoramento de saúde**: Status de APIs externas e conexões
- **Debug em produção**: Logs estruturados e rastreamento de erros

### 📖 Documentação Detalhada
- [🤖 Bot README](bot/README.md) - Configuração e uso do bot
- [🚲 Firmware Bici](firmware/bici/README.md) - Scanner WiFi
- [🏢 Firmware Central](firmware/central/README.md) - Base de coleta
- [🔗 Firmware Common](firmware/common/README.md) - Protocolo BLE compartilhado
- [🌐 Web Dashboard](web/README.md) - Interface web
- [📊 Estrutura Firebase](docs/FIREBASE.md) - Banco de dados
- [🔧 Scripts Utilitários](scripts/README.md) - Automação

## 🚀 Roadmap e Melhorias

### 🎯 Funcionalidades Implementadas

#### ✅ **Sistema Completo v2.0**
- **Firmware modular**: Arquitetura baseada em estados bem definidos
- **Configuração dinâmica**: Configs por base e por bike via Firebase
- **Sistema de validação**: allowed/pending/blocked para bikes
- **Push automático**: Configurações enviadas via BLE
- **Timestamps precisos**: Central adiciona timestamp de recebimento
- **Self-check**: Diagnóstico automático de hardware
- **LED inteligente**: Padrões visuais de status
- **Buffer persistente**: LittleFS para dados locais

#### ✅ **Bot Telegram Avançado**
- **Sistema de assinaturas**: Notificações personalizadas
- **Cálculo de viagens**: Distância, CO₂, duração automáticos
- **Monitor de estações**: Heartbeats e status das centrais
- **Canal público**: @prarodar_updates com publicações automáticas
- **Geolocalização**: Conversão WiFi → coordenadas via Google API
- **Comandos interativos**: /status, /rota, /bikes, /seguir, etc.

#### ✅ **Dashboard Web**
- **Framework moderno**: Remix com Vite
- **Estrutura inicial**: Componente raiz implementado
- **Licença**: AGPL-3.0
- **Status**: 🚧 Desenvolvimento inicial

### 🔮 Próximas Melhorias

#### **Curto Prazo**
- [ ] **OTA Updates**: Atualização de firmware via Firebase
- [ ] **Cache inteligente**: Otimização de chamadas Google API
- [ ] **Relatórios mensais**: Envio automático para assinantes
- [ ] **Mapas interativos**: Visualização de rotas no dashboard

#### **Médio Prazo**
- [ ] **API REST**: Endpoints para integração externa
- [ ] **Mesh Network**: Comunicação entre centrais
- [ ] **Edge Analytics**: Processamento local de dados
- [ ] **Sistema de gamificação**: Ranking de usuários

#### **Longo Prazo**
- [ ] **Machine Learning**: Predição de demanda e rotas
- [ ] **App mobile nativo**: Aplicativo dedicado
- [ ] **Integração IoT**: Sensores adicionais (GPS, acelerômetro)
- [ ] **Blockchain**: Sistema de recompensas descentralizado

## 🤝 Contribuição

### 🛠️ Como Contribuir
1. **Fork** o projeto
2. **Clone** seu fork: `git clone https://gitcentral.com/seu-usuario/bpr-sistema.git`
3. **Crie uma branch**: `git checkout -b feature/nova-funcionalidade`
4. **Faça suas mudanças** seguindo os padrões do projeto
5. **Teste** suas alterações: `npm test` (bot) ou `pio test` (firmware)
6. **Commit** suas mudanças: `git commit -m "feat: adiciona nova funcionalidade"`
7. **Push** para sua branch: `git push origin feature/nova-funcionalidade`
8. **Abra um Pull Request** com descrição detalhada

### 📋 Padrões de Código
- **JavaScript**: ESLint + Prettier
- **C++**: Formato PlatformIO padrão
- **Commits**: Conventional Commits (feat, fix, docs, etc.)
- **Documentação**: Sempre atualizar README.md relevante

### 🐛 Reportar Bugs
1. Verifique se o bug já foi reportado nas [Issues](https://gitcentral.com/projeto/bpr-sistema/issues)
2. Crie uma nova issue com:
   - Descrição clara do problema
   - Passos para reproduzir
   - Comportamento esperado vs atual
   - Screenshots/logs se aplicável
   - Ambiente (OS, versão Node.js, etc.)

## 📄 Licença

**AGPL-3.0 License** - veja o arquivo [LICENSE](LICENSE) para detalhes.

### 📞 Contato e Suporte
- **Issues**: [Gitcentral Issues](https://gitcentral.com/projeto/bpr-sistema/issues)
- **Discussões**: [Gitcentral Discussions](https://gitcentral.com/projeto/bpr-sistema/discussions)
- **Email**: contato@prarodar.org
- **Telegram**: [@prarodarbot](https://t.me/prarodarbot)