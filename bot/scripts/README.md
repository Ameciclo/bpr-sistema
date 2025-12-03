# Scripts

## 📁 Estrutura

### `/webhook`
Scripts para configuração e teste de webhooks do Telegram:
- `check-webhook.js` - Verifica configuração atual do webhook
- `set-webhook.js` - Configura webhook do bot
- `setup-webhook.js` - Script de configuração inicial
- `test-webhook.js` - Testa webhook com mensagem simulada

### `/test`
Scripts de teste e validação:
- `test-geolocation.js` - Testa API de geolocalização
- `test-message.js` - Testa envio de mensagens
- `check-env.js` - Valida variáveis de ambiente

### `/setup`
Scripts de configuração inicial:
- `setup-firebase-config.js` - Configura Firebase
- `setup-firebase-env.sh` - Configura variáveis de ambiente

### `/data`
Scripts para processamento de dados:
- Scripts de migração e processamento de dados das bikes

## 🚀 Como usar

Execute os scripts a partir da raiz do projeto:

```bash
# Exemplo: testar webhook
node scripts/webhook/test-webhook.js

# Exemplo: verificar variáveis de ambiente  
node scripts/test/check-env.js
```