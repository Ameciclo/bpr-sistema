# BPR Sistema - Monorepo

Sistema completo de monitoramento de bicicletas com WiFi scanning, bot Telegram e interface web.

## 📁 Estrutura do Projeto

```
bpr-sistema/
├── firmware/           # Códigos para ESP8266/ESP32
│   ├── bike/          # Firmware da bicicleta (WiFi scanner)
│   └── central/       # Firmware da central/base
├── bot/               # Bot do Telegram (@prarodarbot)
├── web/               # Site em Remix (botaprarodar)
├── shared/            # Código/configs compartilhados
├── docs/              # Documentação geral
└── scripts/           # Scripts de deploy/build
```

## 🚀 Componentes

### 🚲 Firmware Bicicleta
- Scanner WiFi automático
- Upload para Firebase
- Interface web de configuração
- Monitoramento de bateria

### 🏢 Firmware Central
- Ponto de acesso WiFi
- Coleta de dados das bicicletas
- Sincronização com servidor
- LED inteligente de status
- Sistema de configuração dinâmica
- Heartbeat automático para Firebase

#### 🚨 Sistema de LED (ESP32C3 SuperMini)
- **Inicializando**: Piscar rápido (100ms)
- **BLE Ativo**: Piscar lento (2s)
- **Bike Chegou**: 3 piscadas rápidas
- **Bike Saiu**: 1 piscada longa
- **Contagem**: N piscadas = N bikes (a cada 30s)
- **Sincronizando**: Piscar médio (500ms)
- **Erro**: Piscar muito rápido (50ms)

#### ⚙️ Sistema de Configuração da Central

##### **Configuração Inicial:**
```bash
cd firmware/central
./setup.sh  # Configura WiFi, Firebase e ID da base
pio run --target uploadfs  # Upload configuração
pio run --target upload     # Upload firmware
```

##### **Fluxo de Configuração:**
1. **Setup Script** → Cria `config.json` básico com WiFi e `central.id`
2. **Primeira Execução** → Central lê `central.id` e baixa config completa do Firebase
3. **Auto-Update** → Substitui config básica pela completa automaticamente
4. **Funcionamento** → Usa configurações dinâmicas para todos os parâmetros

##### **Configurações Disponíveis:**
- **base_id** - Identificador único da central (ameciclo, cepas, ctresiste)
- **sync_interval_sec** - Intervalo de sincronização (padrão: 300s)
- **wifi_timeout_sec** - Timeout de conexão WiFi (padrão: 30s)
- **led_pin** - Pino do LED de status (padrão: 8)
- **firebase_batch_size** - Tamanho máximo do batch (padrão: 8000 bytes)
- **ntp_server** - Servidor NTP (padrão: pool.ntp.org)
- **timezone_offset** - Fuso horário em segundos (padrão: -10800 = GMT-3)
- **led.*** - Configurações específicas de cada padrão de LED

##### **Estrutura no Firebase:**
```
/central_configs/
├── ameciclo.json    # Config completa da Ameciclo
├── cepas.json       # Config completa da CEPAS
└── ctresiste.json   # Config completa da CTResiste
```

##### **Upload das Configurações:**
```bash
cd scripts
node upload_central_configs.js  # Sobe configs para Firebase
```

##### **Heartbeat Automático:**
Cada central envia heartbeat para `/bases/{base_id}/last_heartbeat` contendo:
- **timestamp** - Quando foi enviado
- **bikes_connected** - Quantas bikes estão conectadas
- **heap** - Memória livre (para debug)
- **uptime** - Tempo ligada em segundos

##### **Vantagens:**
- ✅ **Configuração remota** - Muda parâmetros sem acesso físico
- ✅ **Específica por base** - Cada central tem suas configurações
- ✅ **Fallbacks seguros** - Valores padrão se não conseguir baixar
- ✅ **Auto-sincronização** - Download automático de atualizações
- ✅ **Monitoramento** - Heartbeat para verificar status

### 🤖 Bot Telegram (@prarodarbot)
- **Monitoramento em tempo real** de bicicletas via Firebase
- **Notificações automáticas** de chegada/saída da base
- **Geolocalização WiFi** usando Google Geolocation API
- **Comandos interativos** para consulta de status e rotas
- **Cálculo de distâncias** percorridas baseado em pontos WiFi
- **Interface conversacional** para usuários e administradores

#### 🎯 Funcionalidades Principais
- ✅ **Chegada na base**: Notifica quando bike se conecta ao WiFi da base
- 🚀 **Saída da base**: Notifica quando bike inicia nova sessão de coleta
- 📡 **Scans WiFi**: Mostra redes detectadas durante o percurso em tempo real
- 📍 **Geolocalização**: Converte dados WiFi em coordenadas usando Google API
- 📏 **Cálculo de rota**: Calcula distância percorrida baseada nos pontos coletados
- 🔋 **Status de bateria**: Monitora nível de bateria das bicicletas
- 📊 **Estatísticas**: Resumos de sessões, scans coletados e conexões

#### 🤖 Comandos Disponíveis
- `/start` - Mensagem de boas-vindas e instruções
- `/status [bike]` - Status atual de uma bicicleta específica
- `/rota [bike]` - Última rota calculada com distância percorrida
- `/bikes` - Lista todas as bicicletas monitoradas
- `/help` - Ajuda completa com todos os comandos
- `/ping` - Teste de funcionamento do bot

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
    [timestamp, [ssid, bssid, rssi, channel]]
  ],
  "battery": [[time, level]],
  "connections": [[time, event, base, ip]]
}
```

### 🌐 Site Web
- Dashboard de monitoramento
- Gestão de bicicletas
- Relatórios e análises

## 🛠️ Desenvolvimento

### Pré-requisitos
- **PlatformIO** (para firmware ESP8266/ESP32)
- **Node.js 18+** (para bot e web)
- **Firebase CLI** (para deploy de functions)
- **Google Cloud Account** (para Geolocation API)

### Setup Inicial
```bash
# Clone o repositório
git clone <repo-url>
cd bpr-sistema

# Setup firmware
cd firmware/bike
pio run

# Setup bot
cd ../../bot
npm install
cp .env.example .env  # Configurar variáveis
node scripts/test/check-env.js  # Testar configuração

# Setup web
cd ../web
npm install
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

### 🚲 Firmware
```bash
cd firmware/bike
./setup.sh                    # Configurar WiFi e Firebase
pio run --target uploadfs     # Upload sistema de arquivos
pio run --target upload       # Upload código
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
npm run deploy  # Vercel/Netlify
```

## 🔗 Integrações

Todos os componentes se integram via:

### 🔥 Firebase Realtime Database
- **Firmware** → Escreve dados de scans e status
- **Bot** → Escuta mudanças em tempo real
- **Web** → Dashboard com dados consolidados
- **Configurações** → Centralizadas por base/bike

### 🌐 APIs Externas
- **Google Geolocation API** → Conversão WiFi para coordenadas
- **Telegram Bot API** → Notificações e comandos
- **NTP Servers** → Sincronização de horário

### 📡 Fluxo de Dados
```mermaid
graph TB
    subgraph "🚲 Bicicleta (ESP32)"
        A[WiFi Scanner] --> B[Coleta Redes]
        B --> C[Armazena Local]
        C --> D[Detecta Base]
        D --> E[Upload Firebase]
    end
    
    subgraph "🔥 Firebase"
        E --> F[Realtime Database]
        F --> G[/bikes/{id}/sessions]
        F --> H[/bikes/{id}/status]
    end
    
    subgraph "🤖 Bot Telegram"
        G --> I[Monitor Sessões]
        H --> J[Monitor Status]
        I --> K[Nova Sessão?]
        J --> L[Mudança Status?]
        K --> M[Processar Scans]
        L --> N[Notificar Admin]
        M --> O[Google Geolocation]
        O --> P[Calcular Posição]
        P --> Q[Enviar Notificação]
    end
    
    subgraph "👤 Usuários"
        Q --> R[Telegram Chat]
        R --> S[Comandos]
        S --> T[/status /rota /bikes]
        T --> U[Consultar Firebase]
        U --> V[Resposta Formatada]
    end
    
    subgraph "🌐 Dashboard Web"
        F --> W[Interface Visual]
        W --> X[Mapas e Gráficos]
    end
```

## 🗄️ Estrutura de Dados (Firebase)

### 🤖 Dados Específicos do Bot

O bot monitora e processa os seguintes dados em tempo real:

#### `/bikes/{bikeId}/sessions/{sessionId}` - Sessões Ativas
```json
{
  "start": 1733459200,
  "end": null,
  "mode": "normal",
  "scans": [
    [1733459205, [["NET_5G", "AA:BB:CC:11:22:33", -70, 6]]],
    [1733459230, [["CLARO_WIFI", "CC:DD:EE:44:55:66", -82, 11]]]
  ],
  "battery": [[9685, 82], [9710, 81]],
  "connections": [
    [1733459195, "connect", "BASE_WIFI_1", "192.168.252.4"],
    [1733459200, "disconnect", "BASE_WIFI_1", null]
  ]
}
```

#### Processamento pelo Bot
1. **Monitor de Sessões**: Escuta `child_changed` em `/bikes`
2. **Monitor de Scans**: Escuta `child_added` em `/bikes/{id}/sessions/{session}/scans`
3. **Geolocalização**: Converte arrays WiFi para coordenadas via Google API
4. **Notificações**: Envia alertas automáticos para admin
5. **Comandos**: Responde consultas de usuários com dados processados

## 🗄️ Estrutura de Dados (Firebase)

O Firebase Realtime Database é estruturado como uma árvore JSON otimizada para:
- Leituras rápidas
- Escritas baratas
- Acesso simultâneo (central, bicicletas, dashboard, bot)
- Caching local nos ESP32

### Principais Coleções:

#### `/config` - Configurações Globais
```json
{
  "config": {
    "version": 3,
    "wifi_scan_interval_sec": 25,
    "wifi_scan_interval_low_batt_sec": 60,
    "deep_sleep_after_sec": 300,
    "ble_ping_interval_sec": 5,
    "min_battery_voltage": 3.45,
    "update_timestamp": 1733459200
  }
}
```

#### `/central_configs` - Configurações por Central
```json
{
  "base01": {
    "base_id": "base01",
    "sync_interval_sec": 300,
    "wifi_timeout_sec": 30,
    "led_pin": 8,
    "ntp_server": "pool.ntp.org",
    "timezone_offset": -10800,
    "firebase_batch_size": 8000,
    "led": {
      "boot_ms": 100,
      "ble_ready_ms": 2000,
      "wifi_sync_ms": 500
    }
  }
}
```

#### `/bases` - Centrais/Bases
```json
{
  "bases": {
    "base01": {
      "name": "Base Centro",
      "max_bikes": 10,
      "wifi_ssid": "BPR_Base01",
      "wifi_password": "senha_base01",
      "location": {
        "lat": -8.062,
        "lng": -34.881
      },
      "last_sync": 1733459210
    }
  }
}
```

#### `/bikes` - Bicicletas
```json
{
  "bikes": {
    "bike07": {
      "base_id": "base01",
      "uid": "bike07",
      "battery_voltage": 3.82,
      "status": "active",
      "last_ble_contact": 1733459190,
      "last_wifi_scan": 1733459205,
      "last_position": {
        "lat": -8.064,
        "lng": -34.882,
        "source": "wifi",
        "accuracy": 50
      },
      "metrics": {
        "km_total": 213.4,
        "rides_total": 58
      },
      "sessions": {
        "session_1733459200": {
          "start": 1733459200,
          "end": null,
          "mode": "normal",
          "scans": [...],
          "battery": [...],
          "connections": [...]
        }
      }
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
├── 🚲 firmware/           # Códigos ESP8266/ESP32
│   ├── bike/              # Scanner WiFi da bicicleta
│   └── central/           # Base/central de coleta
├── 🤖 bot/                # Bot Telegram (@prarodarbot)
│   ├── src/               # Código principal
│   ├── functions/         # Firebase Functions (opcional)
│   ├── scripts/           # Utilitários e testes
│   └── docs/              # Documentação específica
├── 🌐 web/                # Dashboard web (Remix)
├── 📊 shared/             # Configurações compartilhadas
├── 📚 docs/               # Documentação geral
└── 🔧 scripts/            # Scripts de deploy/build
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
- [🚲 Firmware Bike](firmware/bike/README.md) - Scanner WiFi
- [🏢 Firmware Central](firmware/central/README.md) - Base de coleta
- [🌐 Web Dashboard](web/README.md) - Interface web
- [📊 Estrutura Firebase](docs/FIREBASE.md) - Banco de dados
- [🔧 Scripts Utilitários](scripts/README.md) - Automação

## 🚀 Roadmap e Melhorias

### 🎯 Curto Prazo (1-2 meses)
- [ ] **Cache inteligente**: Evitar chamadas desnecessárias à Google API
- [ ] **Filtros de qualidade**: Ignorar redes WiFi com RSSI muito baixo
- [ ] **Alertas de bateria**: Notificações quando bateria < 20%
- [ ] **Timeout inteligente**: Alertar se bike não se conecta há muito tempo
- [ ] **Múltiplas bikes**: Suporte automático para novas bicicletas

### 🎨 Médio Prazo (3-6 meses)
- [ ] **Dashboard web integrado**: Interface visual para monitoramento
- [ ] **Histórico de rotas**: Armazenar e consultar trajetos anteriores
- [ ] **Estatísticas avançadas**: Relatórios de uso, distância total, CO2 economizado
- [ ] **Integração com mapas**: Visualização de rotas no Google Maps
- [ ] **API REST**: Endpoints para integração com outros sistemas

### 🔮 Longo Prazo (6+ meses)
- [ ] **Machine Learning**: Predição de rotas e padrões de uso
- [ ] **Alertas inteligentes**: Detecção automática de anomalias
- [ ] **App mobile nativo**: Aplicativo dedicado para usuários
- [ ] **Integração IoT**: Sensores adicionais (GPS, acelerômetro, etc.)
- [ ] **Blockchain**: Sistema de recompensas por uso sustentável

## 🤝 Contribuição

### 🛠️ Como Contribuir
1. **Fork** o projeto
2. **Clone** seu fork: `git clone https://github.com/seu-usuario/bpr-sistema.git`
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
1. Verifique se o bug já foi reportado nas [Issues](https://github.com/projeto/bpr-sistema/issues)
2. Crie uma nova issue com:
   - Descrição clara do problema
   - Passos para reproduzir
   - Comportamento esperado vs atual
   - Screenshots/logs se aplicável
   - Ambiente (OS, versão Node.js, etc.)

## 📄 Licença

**MIT License** - veja o arquivo [LICENSE](LICENSE) para detalhes.

### 📞 Contato e Suporte
- **Issues**: [GitHub Issues](https://github.com/projeto/bpr-sistema/issues)
- **Discussões**: [GitHub Discussions](https://github.com/projeto/bpr-sistema/discussions)
- **Email**: contato@prarodar.org
- **Telegram**: [@prarodarbot](https://t.me/prarodarbot)