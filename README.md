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

## 📚 Documentação

Veja a pasta `docs/` para documentação detalhada de cada componente.