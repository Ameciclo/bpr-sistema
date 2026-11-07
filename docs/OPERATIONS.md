# Operação — Build, Configuração, Deploy e Troubleshooting

## 1. Pré-requisitos

| Item | Versão mínima | Uso |
|---|---|---|
| PlatformIO | 6.x | Firmware ESP32-C3 |
| Node.js | 18+ (Functions: 22) | Bot + web |
| Firebase CLI | qualquer recente | Deploy functions + rules |
| Conta Google Cloud | ativa | Geolocation API (chave server-side) |
| Telegram | — | Bot + canal |
| Hardware | 2× Seeed XIAO ESP32C3 (bike + central), bateria 18650 | Campo |

## 2. Setup e execução local

```bash
# Raiz (workspaces npm: bot, web + functions)
npm install
npm install --workspaces   # se necessário
cd bot/functions && npm install

# Firmware
cd firmware/bici    && pio run --target upload && pio run --target uploadfs
cd firmware/central && pio run --target upload && pio run --target uploadfs
pio device monitor   # serial 115200

# Bot polling
cd bot && cp .env.example .env   # preencher credenciais
npm run dev    # nodemon
node scripts/test/check-env.js   # valida presença das variáveis

# Functions (local)
cd bot/functions && npm run build && npm run serve   # emulador

# Web
cd web && npm install && npm run dev   # http://localhost:5173
```

### Diagnóstico rápido

```bash
./diagnose.sh    # verifica PlatformIO, USB e portas seriais
```

## 3. Configuração do ambiente (bot)

`bot/.env.example` (copie para `.env`):

```bash
TELEGRAM_BOT_TOKEN=...          # @BotFather
ADMIN_CHAT_ID=...               # chat do administrador
PUBLIC_CHANNEL_ID=@prarodar_updates

FIREBASE_PROJECT_ID=...
FIREBASE_DATABASE_URL=https://<proj>.firebaseio.com
FIREBASE_PRIVATE_KEY="-----BEGIN PRIVATE KEY-----\n..."
FIREBASE_CLIENT_EMAIL=...@project.iam.gserviceaccount.com

GOOGLE_GEOLOCATION_API_KEY=...
```

**Bot polling** (`bot/src/index.js`) valida `TELEGRAM_BOT_TOKEN`,
`FIREBASE_PROJECT_ID/DATABASE_URL/PRIVATE_KEY/CLIENT_EMAIL` no boot e aborta sem
credenciais. Processa comandos: `/start /help /ping /bikes /status [bike] /rota [bike]
/estacao [id] /seguir /parar /minhas`.

**Functions** (`bot/functions/src/index.ts`) usa `functions.config()` legacy:

```bash
firebase functions:config:set \
  telegram.bot_token="..." telegram.admin_chat_id="..." telegram.webhook_secret="..." \
  google.api_key="..."
```

Webhook validado por `x-telegram-bot-api-secret-token` (se configurado). Comandos printados: `/status /rota /bikes /converter` (cooldown 60 s — API com custo).

> ⚠️ **Não execute polling e Functions simultaneamente** — ambos notificarão os mesmos
> eventos e o admin receberá alertas duplicados.

## 4. Configuração da central (primeira execução)

1. Flash firmware + upload filesystem (empty `config_credentials.bin.json` inicial).
2. Central boota sem credenciais → `INITIAL_CONFIG_AP`.
3. Conecte-se ao AP **`BPR Central`** (senha **`botaprarodar`**), acesse
   `http://192.168.4.1`.
4. Informe `base_id`, Wi-Fi SSID/senha, `databaseURL`, `firebase_api_key`.
5. Salve → reinicia → `INITIAL_SYNC` obrigatória.
6. Sucesso → `BIKE_PAIRING` (ciclo padrão: `BIKE_PAIRING` ↔ `CLOUD_SYNC`).

```mermaid
flowchart LR
    A[Energizar central] --> B{Credenciais válidas?}
    B -->|Não| AP[INITIAL_CONFIG_AP<br/>BPR Central / 192.168.4.1]
    AP -->|salvar| S[Reinicia → INITIAL_SYNC]
    B -->|Sim, primeiro sync| S
    B -->|Sim| P[BIKE_PAIRING]
    S --> P
    P --> C[CLOUD_SYNC a cada sync_interval]
    C --> P
    P --> AP2[TEMP_CONFIG_AP após N falhas]
    AP2 --> P
```

O portal aceita formulário ou JSON bruto. Valida campos obrigatórios antes de salvar
(`ConfigAP::update` / `ConfigCredentials::load`). Timeout de config: 15 min
(`CONFIG_TIMEOUT_MS`) no estado obrigatório; temporário usa `config_ap_timeout_sec` (300 s padrão).

## 5. Deploy

### 5.1 Firmware

```bash
# Bike
cd firmware/bici
pio run -e seeed_xiao_esp32c3
pio run -e seeed_xiao_esp32c3 --target upload
pio run -e seeed_xiao_esp32c3 --target uploadfs    # data/config.json
pio device monitor

# Central
cd firmware/central
pio run -e seeed_xiao_esp32c3
pio run -e seeed_xiao_esp32c3 --target upload
pio run -e seeed_xiao_esp32c3 --target uploadfs
pio device monitor
```

**Importante:** reflashe **bike + central juntas** se mudar `common/bpr_protocol.h`
(quebra silenciosa por UUID).

### 5.2 Bot polling — produção

```bash
cd bot
npm install --production
pm2 start src/index.js --name prarodarbot
```

Alternativas: Docker (`docker build -t prarodarbot .`) ou VPS com `npm start`.
Logs estruturados via stdout; erros de Telegram são capturados (`bot.catch`).

### 5.3 Bot Functions

```bash
cd bot/functions
npm run lint && npm run build
firebase deploy --only functions
```

Triggers registrados:

| Export | Trigger | Função |
|---|---|---|
| `onBatchUploaded` | `/bases/{baseId}/data/{batchId}` (onWrite) | Normaliza fragmentos → `sessions_v2` |
| `onSessionAssembled` | `/bikes/{bikeId}/sessions_v2/{sessionKey}` (onCreate) | Notificação admin |
| `telegramBot` | HTTPS `onRequest` | Webhook (`/status /rota /bikes /converter`) |

### 5.4 Web

```bash
cd web && npm run build && npm run start
```

Deploy em Vercel/Netlify/Firebase Hosting (config manual). Todas as páginas
são placeholder; não há build que consuma Firebase ainda.

## 6. Firebase — exemplo de popularização mínima

Usando `config.example.json` como referência, o banco deve conter ao menos:

```json
{
  "bases": { "base01": { "configs": { "base_id": "base01", "sync_interval_sec": 90 }, "last_heartbeat": { "...": "..." } } },
  "bike_configs": { "bpr-000123": { "bike_name": "Bike 1", "wifi": { "scan_interval_sec": 300 } } },
  "subscriptions": { "123": { "bikes": ["bpr-000123"], "stations": ["base01"], "system": false } }
}
```

**Rules** (`bot/database.rules.json`): leitura pública de `bikes`, `bases`, `rides`,
`public_stats`; escrita apenas `auth != null`; assinaturas só pelo dono.
A central usa **REST API com `auth=API_KEY`** (não `service_account`), logo as
rules devem ser compatíveis com acesso por chave legada de API — valide em produção.

## 7. Troubleshooting

### 7.1 Firmware — bike não conecta

- **Confirmar nome BLE:** bike procura exatamente `BPR Central`
  (`CENTRAL_BLE_NAME`/`BLE_DEVICE_NAME`). Central anuncia esse nome.
- **UUIDs:** `87654321...` (data) e `11111111...` (config) nos dois lados.
- central may be in `CLOUD_SYNC` — ela sinaliza scan restrito + `busy`.
- Bike could be `blocked` — desconexão forçada no lado da central.

Ver serial: `🔵 Searching for BPR Central…`, `✅ Connected`, `📤 Uploading…`,
`✅ Data confirmed`. Se `AT_BASE` estourar em 300 s, force `SLEEP` e tente em 30 s.

### 7.2 Dados não chegam no Firebase

- Bike `allowed`? `pending` só grava visita — dados **ignorados** por design.
- `wifi_data` fragmento precisa de `is_final` presente para a Functions publicar;
  seq gap = espera indefinida.
- `downloadCentralConfig`/`downloadBikeConfigs` com rede lenta pode trazer versão
  antiga — compare `last_update`.

### 7.3 Central não sincroniza

- Credenciais inválidas → `INITIAL_CONFIG_AP` (AP sempre ativo sem timeout).
- Sync falha repetida → `TEMP_CONFIG_AP` após 5 falhas (`SyncMonitor::shouldFallback`),
  retry com backoff 30 s (`CloudSync::retry`).
- Verificar roteamento: a URL REST contém `?auth=`; logs **redigem** a credencial
  (`redactUrl`).

### 7.4 Bot não notifica / rota vazia

- `isOnline=null` = heartbeat inválido/ausente — bot não inventa status (ver `stationMonitor`).
- Viagens <80 m são descartadas.
- Scan >10 min antigo é marcado `isHistorical` e não gera notificação de "movimento agora".
- Scans sem `bssid` são filtrados antes da Google API (API rejeita o ponto).

### 7.5 LED da central (Pin 8)

| Padrão | Significado |
|---|---|
| Boot (100 ms) | inicialização |
| Config AP (200 ms) | modo config ativo |
| BLE ready (2000 ms) | aguardando bikes |
| Bike arrived (3×150 ms) | bike conectou |
| Bike left (800 ms) | bike desconectou |
| Count (N piscadas) | N bikes conectadas (a cada 30 s) |
| Sync (500 ms) | sincronizando Firebase |
| Error (50 ms) | erro crítico |

## 8. Comandos úteis

```bash
# Testes bot
node bot/scripts/test/check-env.js
node bot/scripts/test/test-geolocation.js
node bot/scripts/test/test-message.js

# Webhook
node bot/scripts/webhook/set-webhook.js
node bot/scripts/webhook/check-webhook.js

# Decodificar amostras legadas da central
node decode_hub_data.js

# Gerar GeoJSON de rotas
node bot/scripts/data/generate-geojson.js
```

## 9. Segurança

- Bancos com regras restritas; credenciais em `.env`; service account via env.
- `functions.config()` armazena segredos de produção; webhook valida `secret_token`.
- Logs do firmware evitam imprimir URLs com `?auth=` (`redactUrl`).
- Coordenadas públicas são aproximadas (3 casas ≈ 100 m); canal público recebe
  dados agregados, não trajetos precisos. Assinaturas por `userId` Telegram.

## 10. Contribuição

1. Fork, branch `feature/nova-funcionalidade`, conventional commits.
2. Padrões: ESP (ArduinoJson 6.x, structs packed), JS (CommonJS no polling, TS nas Functions).
3. Atualize README/ADR afetado em `docs/`.
4. Teste firmware com serial + binário de divisão de sessão; teste bot com `check-env` +
   envio de mensagem.

Licença: AGPL-3.0 (`LICENSE`).
