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

### 🤖 Bot Telegram
- Notificações automáticas
- Comandos de controle
- Interface de usuário

### 🌐 Site Web
- Dashboard de monitoramento
- Gestão de bicicletas
- Relatórios e análises

## 🛠️ Desenvolvimento

### Pré-requisitos
- PlatformIO (para firmware)
- Node.js (para bot e web)
- Firebase CLI

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

# Setup web
cd ../web
npm install
```

## 📦 Deploy

Cada componente tem seu próprio processo de deploy:
- **Firmware**: PlatformIO upload
- **Bot**: Deploy no servidor
- **Web**: Deploy na Vercel/Netlify

## 🔗 Integrações

Todos os componentes se integram via:
- Firebase Realtime Database
- APIs REST compartilhadas
- Configurações centralizadas

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
        "source": "wifi"
      },
      "metrics": {
        "km_total": 213.4,
        "rides_total": 58
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

Veja a pasta `docs/` para documentação detalhada de cada componente.