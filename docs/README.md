# Documentação BPR Sistema

Documentação completa do sistema de monitoramento de bicicletas.

## 📚 Índice

### 🚲 Firmware
- [Configuração Inicial](../firmware/bike/README.md)
- [Monitoramento de Bateria](../firmware/bike/BATTERY_MONITORING.md)
- [Estrutura de Dados](../firmware/bike/ESTRUTURA_OTIMIZADA.md)
- [Fluxos do Sistema](../firmware/bike/SYSTEM_FLOWS.md)

### 🤖 Bot Telegram
- [Comandos Disponíveis](bot-commands.md)
- [Configuração de Alertas](bot-alerts.md)

### 🌐 Interface Web
- [Guia do Usuário](web-user-guide.md)
- [API Reference](web-api.md)

### 🔧 Desenvolvimento
- [Setup do Ambiente](development-setup.md)
- [Contribuindo](contributing.md)
- [Deploy](deployment.md)

## 🏗️ Arquitetura

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│   Bicicleta │    │   Central   │    │  Firebase   │
│    (ESP)    │───▶│   (ESP32)   │───▶│  Database   │
└─────────────┘    └─────────────┘    └─────────────┘
                                             │
                   ┌─────────────┐          │
                   │ Bot Telegram│◀─────────┤
                   └─────────────┘          │
                                             │
                   ┌─────────────┐          │
                   │  Web App    │◀─────────┘
                   └─────────────┘
```

## 🚀 Quick Start

1. **Clone o repositório**
   ```bash
   git clone <repo-url>
   cd bpr-sistema
   ```

2. **Configure o firmware**
   ```bash
   cd firmware/bike
   # Edite data/config.txt
   pio run --target uploadfs
   pio run --target upload
   ```

3. **Configure o bot** (futuro)
   ```bash
   cd bot
   npm install
   npm run dev
   ```

4. **Configure o web** (futuro)
   ```bash
   cd web
   npm install
   npm run dev
   ```

## 📞 Suporte

- Issues: GitHub Issues
- Documentação: Esta pasta
- Contato: [seu-email]