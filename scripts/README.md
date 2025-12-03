# Scripts de Automação

Scripts para facilitar o desenvolvimento e deploy do sistema BPR.

## 📋 Scripts Disponíveis

### 🔧 Desenvolvimento
- `setup.sh` - Setup inicial completo
- `dev-firmware.sh` - Desenvolvimento do firmware
- `dev-bot.sh` - Desenvolvimento do bot
- `dev-web.sh` - Desenvolvimento do web

### 🚀 Deploy
- `deploy-firmware.sh` - Deploy do firmware
- `deploy-bot.sh` - Deploy do bot
- `deploy-web.sh` - Deploy do web
- `deploy-all.sh` - Deploy completo

### 🧪 Testes
- `test-firmware.sh` - Testes do firmware
- `test-integration.sh` - Testes de integração

## 🎯 Uso

```bash
# Setup inicial
./scripts/setup.sh

# Desenvolvimento
./scripts/dev-firmware.sh
./scripts/dev-bot.sh
./scripts/dev-web.sh

# Deploy
./scripts/deploy-all.sh
```

## ⚙️ Configuração

Os scripts usam variáveis de ambiente definidas em `.env` na raiz do projeto.