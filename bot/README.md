# BPR Telegram Bot (@prarodarbot)

Bot do Telegram para notificações e controle do sistema de bicicletas.

## Funcionalidades (Planejadas)

- 🔔 Notificações automáticas
  - Bicicleta fora da área
  - Bateria baixa
  - Problemas de conectividade
- 📊 Comandos de consulta
  - Status das bicicletas
  - Localização atual
  - Histórico de uso
- ⚙️ Comandos administrativos
  - Configurar alertas
  - Gerenciar usuários
  - Relatórios

## Tecnologias

- Node.js
- Telegraf.js
- Firebase Admin SDK
- TypeScript

## Configuração

```bash
npm install
cp .env.example .env
# Configure as variáveis no .env
npm run dev
```

## Comandos Planejados

```
/status - Status geral das bicicletas
/bike <id> - Status de uma bicicleta específica
/alerts - Configurar alertas
/help - Ajuda
```

## Status

🚧 **Em desenvolvimento** - Bot será implementado após finalização do firmware.

## Estrutura

```
bot/
├── src/
│   ├── commands/     # Comandos do bot
│   ├── services/     # Integração Firebase
│   ├── utils/        # Utilitários
│   └── index.ts      # Entrada principal
├── package.json
└── .env.example
```