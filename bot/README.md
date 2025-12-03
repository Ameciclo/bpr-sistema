# 🚴 Pra Rodar Bot

Bot do Telegram para monitoramento em tempo real de bicicletas compartilhadas que coletam dados de redes WiFi durante o percurso.

## 🎯 Funcionalidades

### Monitoramento Automático
- ✅ **Chegada na base**: Notifica quando a bicicleta se conecta a uma rede WiFi da base
- 🚀 **Saída da base**: Notifica quando a bicicleta se desconecta e inicia coleta
- 📡 **Coleta WiFi**: Mostra redes WiFi detectadas durante o percurso
- 📍 **Geolocalização**: Converte dados WiFi em coordenadas usando Google Geolocation API
- 📏 **Cálculo de distância**: Calcula a distância percorrida baseada nos pontos coletados

### Comandos do Bot
- `/start` - Mensagem de boas-vindas
- `/status [bike]` - Status atual de uma bicicleta específica
- `/rota [bike]` - Última rota calculada com distância percorrida
- `/bikes` - Lista todas as bicicletas monitoradas
- `/help` - Ajuda com todos os comandos

## 🛠️ Configuração

### 1. Instalar dependências
```bash
npm install
```

### 2. Configurar variáveis de ambiente
Copie o arquivo `.env.example` para `.env` e preencha as variáveis:

```bash
cp .env.example .env
```

#### Variáveis obrigatórias:

**Telegram Bot:**
- `TELEGRAM_BOT_TOKEN` - Token do bot (obtenha com @BotFather)
- `ADMIN_CHAT_ID` - ID do chat para receber notificações automáticas

**Firebase:**
- `FIREBASE_PROJECT_ID` - ID do projeto Firebase
- `FIREBASE_DATABASE_URL` - URL do Realtime Database
- `FIREBASE_PRIVATE_KEY` - Chave privada da service account
- `FIREBASE_CLIENT_EMAIL` - Email da service account

**Google Geolocation API:**
- `GOOGLE_GEOLOCATION_API_KEY` - Chave da API de geolocalização

### 3. Executar o bot
```bash
# Desenvolvimento
npm run dev

# Produção
npm start

# Testar configuração
node scripts/test/check-env.js

# Configurar webhook
node scripts/webhook/set-webhook.js
```

## 📊 Estrutura dos Dados

### Scans (Firebase)
```json
{
  "scans": {
    "1760209736": {
      "bike": "intenso",
      "timestamp": 1760209736,
      "networks": [
        {
          "ssid": "BASE_WIFI_1",
          "rssi": -34,
          "channel": 6
        }
      ]
    }
  }
}
```

### Status (Firebase)
```json
{
  "status": {
    "intenso": {
      "bike": "intenso",
      "lastUpdate": 1760210106,
      "battery": [
        {
          "level": 82,
          "time": 9685
        }
      ],
      "connections": [
        {
          "event": "connect",
          "base": "BASE_WIFI_1",
          "ip": "192.168.252.4",
          "time": 1760210095
        }
      ]
    }
  }
}
```

## 🔧 Estrutura do Projeto

```
📁 src/                      # Código principal
├── config/
│   └── firebase.js          # Configuração Firebase
├── services/
│   ├── bikeMonitor.js       # Monitoramento das bikes
│   └── geolocation.js       # Serviço de geolocalização
├── utils/
│   └── dataConverter.js     # Utilitários de conversão
└── index.js                 # Bot principal

📁 functions/                # Firebase Functions
├── src/                     # Código TypeScript
└── lib/                     # Código compilado

📁 scripts/                  # Scripts utilitários
├── webhook/                 # Scripts de webhook
├── test/                    # Scripts de teste
├── setup/                   # Scripts de configuração
└── data/                    # Scripts de processamento

📁 docs/                     # Documentação
├── DEPLOY.md               # Guia de deploy
├── FLUXO_LOGICA.md         # Fluxo do sistema
└── fluxo-add-coordinates.md # Processo de coordenadas

📁 tools/                    # Ferramentas auxiliares
├── route-viewer.html        # Visualizador de rotas
├── clean-credentials.js     # Limpeza de credenciais
└── run.sh                   # Scripts de execução
```

### Fluxo de Funcionamento

1. **Coleta de Dados**: Bicicleta coleta redes WiFi durante o percurso
2. **Upload Firebase**: Dados são enviados para Firebase quando a bike chega na base
3. **Monitoramento**: Bot escuta mudanças no Firebase em tempo real
4. **Processamento**: Converte dados WiFi em coordenadas geográficas
5. **Notificação**: Envia alertas automáticos via Telegram
6. **Consulta**: Usuários podem consultar status e rotas via comandos

## 🚀 Melhorias Sugeridas

### Curto Prazo
- [ ] **Cache de localizações**: Evitar chamadas desnecessárias à API do Google
- [ ] **Filtro de redes**: Ignorar redes com RSSI muito baixo
- [ ] **Bateria crítica**: Alertas quando bateria < 20%
- [ ] **Timeout de conexão**: Alertar se bike não se conecta há muito tempo

### Médio Prazo
- [ ] **Dashboard web**: Interface visual para monitoramento
- [ ] **Histórico de rotas**: Armazenar e consultar rotas anteriores
- [ ] **Múltiplas bikes**: Suporte automático para novas bikes
- [ ] **Estatísticas**: Relatórios de uso, distância total, etc.

### Longo Prazo
- [ ] **Machine Learning**: Predição de rotas e padrões de uso
- [ ] **Integração mapas**: Visualização de rotas no Google Maps
- [ ] **API REST**: Endpoint para integração com outros sistemas
- [ ] **Alertas inteligentes**: Detecção de anomalias e problemas

## 🔒 Segurança

- Todas as credenciais ficam no arquivo `.env` (não commitado)
- Firebase configurado com service account
- Rate limiting automático do Telegraf
- Logs não expõem dados sensíveis

## 📝 Logs

O bot gera logs estruturados:
- ✅ Conexões bem-sucedidas
- ❌ Erros e falhas
- 📱 Comandos recebidos
- 🚴 Eventos das bicicletas

## 📚 Documentação Adicional

- [📋 Guia de Deploy](docs/DEPLOY.md) - Opções de deployment
- [🔄 Fluxo da Lógica](docs/FLUXO_LOGICA.md) - Funcionamento interno
- [📜 Scripts Disponíveis](scripts/README.md) - Utilitários e testes

## 🤝 Contribuição

1. Fork o projeto
2. Crie uma branch para sua feature
3. Commit suas mudanças
4. Push para a branch
5. Abra um Pull Request

## 📄 Licença

MIT License - veja o arquivo LICENSE para detalhes.