# 🚀 Deploy Options - Bot de Monitoramento

## Opção 1: Servidor Próprio (Node.js)

### Vantagens:
- ✅ Controle total
- ✅ Sem limites de execução
- ✅ Logs completos
- ✅ Mais barato para uso contínuo

### Deploy:
```bash
# Instalar dependências
npm install

# Configurar .env
cp .env.example .env
# Preencher variáveis

# Testar configuração
node scripts/test/check-env.js

# Configurar webhook
node scripts/webhook/set-webhook.js

# Executar
npm start
```

### Produção (PM2):
```bash
npm install -g pm2
pm2 start src/index.js --name "bike-bot"
pm2 startup
pm2 save
```

---

## Opção 2: Firebase Functions (Serverless)

### Vantagens:
- ✅ Sem servidor para manter
- ✅ Escala automaticamente
- ✅ Integração nativa Firebase
- ✅ Triggers automáticos

### Deploy:
```bash
# Instalar Firebase CLI
npm install -g firebase-tools

# Login
firebase login

# Configurar projeto
firebase init functions

# Configurar variáveis
firebase functions:config:set \
  telegram.bot_token="SEU_BOT_TOKEN" \
  telegram.admin_chat_id="SEU_CHAT_ID" \
  google.geolocation_api_key="SUA_API_KEY"

# Deploy
firebase deploy --only functions
```

### Configurar Webhook:
```bash
# Após deploy, usar script de configuração
node scripts/webhook/set-webhook.js

# Ou manualmente:
curl -X POST "https://api.telegram.org/bot<BOT_TOKEN>/setWebhook" \
  -d "url=https://us-central1-<PROJECT_ID>.cloudfunctions.net/telegramWebhook"

# Verificar configuração
node scripts/webhook/check-webhook.js
```

---

## Comparação de Custos

### Servidor Próprio:
- **VPS básica**: $5-10/mês
- **Uso contínuo**: Fixo
- **Escalabilidade**: Manual

### Firebase Functions:
- **Gratuito**: 2M invocações/mês
- **Pago**: $0.40 por 1M invocações
- **Escalabilidade**: Automática

---

## Recomendação

### Use **Servidor Próprio** se:
- Bot recebe muitas mensagens (>100k/mês)
- Precisa de logs detalhados
- Quer controle total

### Use **Firebase Functions** se:
- Bot tem uso moderado (<50k/mês)
- Quer zero manutenção
- Prefere integração nativa

---

## Estrutura de Arquivos

```
prarodarbot/
├── src/                    # Versão servidor próprio
│   ├── config/
│   ├── services/
│   ├── utils/
│   └── index.js
├── functions/              # Versão Firebase Functions
│   ├── src/                # TypeScript source
│   ├── lib/                # Compiled JS
│   └── package.json
├── scripts/                # Scripts utilitários
│   ├── webhook/            # Configuração webhook
│   ├── test/               # Testes
│   └── setup/              # Configuração inicial
├── docs/                   # Documentação
├── tools/                  # Ferramentas auxiliares
├── firebase.json
├── database.rules.json
└── package.json
```

Ambas as versões funcionam com a mesma estrutura de dados otimizada!