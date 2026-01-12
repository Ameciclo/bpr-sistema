# 🤖 Pra Rodar Bot

Bot inteligente do Telegram para monitoramento completo do sistema de bicicletas compartilhadas com coleta de dados WiFi, geolocalização em tempo real e notificações personalizadas.

> **🎆 Novidade**: Sistema completo de assinaturas, monitor de estações, cálculo automático de viagens e canal público!

## 🎯 Funcionalidades

### 📡 Monitoramento em Tempo Real
- ✅ **Chegada na base**: Detecta quando bike se conecta via BLE
- 🚀 **Saída da base**: Inicia nova sessão de coleta automaticamente
- 📡 **Scans WiFi**: Processa redes detectadas durante o percurso
- 📍 **Geolocalização**: Converte dados WiFi em coordenadas via Google API
- 🗺️ **Cálculo de viagens**: Distância, CO₂ economizado, duração
- 🏢 **Monitor de estações**: Verifica heartbeats e status das bases

### 📱 Notificações Personalizadas
- **Seguir bike específica**: Receba alertas de uma bike escolhida
- **Seguir estação**: Monitore todas as bikes de uma estação
- **Seguir sistema**: Acompanhe todas as atividades
- **Canal público**: Publicações automáticas para todos

### 🤖 Comandos Disponíveis

#### 📊 Consultas
- `/bikes` - Lista bikes disponíveis em todas as estações
- `/status [bike]` - Status detalhado de uma bike
- `/rota [bike]` - Última viagem com mapa e métricas
- `/estacao [id]` - Status de uma estação específica

#### 📱 Assinaturas
- `/seguir [bike]` - Seguir bike específica (ex: `/seguir intenso`)
- `/seguir estacao_[id]` - Seguir estação (ex: `/seguir estacao_base01`)
- `/seguir sistema` - Seguir sistema completo
- `/parar [target]` - Parar de seguir
- `/minhas` - Ver suas assinaturas ativas

#### 🔧 Utilitários
- `/start` - Mensagem de boas-vindas
- `/help` - Ajuda completa
- `/ping` - Teste de funcionamento

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
- `ADMIN_CHAT_ID` - ID do chat para receber notificações administrativas
- `PUBLIC_CHANNEL_ID` - ID do canal público (ex: @prarodar_updates)

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

### 📊 Estrutura de Dados Monitorada

#### Sessões Ativas (`/bikes/{bikeId}/sessions/{sessionId}`)
```json
{
  "bike_id": "bpr-a1b2c3",
  "session_start_millis": 45000,
  "scans": [
    [47000, [["NET_5G", "AA:BB:CC:11:22:33", -70, 6]]],
    [52000, [["CLARO_WIFI", "CC:DD:EE:44:55:66", -82, 11]]]
  ],
  "battery": [[47000, 85], [52000, 84]],
  "connections": [
    [47195, "connect", "BASE_WIFI_1", "192.168.252.4"]
  ],
  "hub_receive_timestamp": 1733459800,
  "hub_receive_timestamp_human": "2024-12-06 10:30:00 UTC-3"
}
```

#### Viagens Concluídas (`/rides/{bikeId}/{rideId}`)
```json
{
  "rides": {
    "intenso": {
      "ride_1733459200": {
        "start_ts": 1733459200,
        "end_ts": 1733461000,
        "km": 2.8,
        "co2_saved_g": 406,
        "duration_min": 30,
        "points_count": 12,
        "route": [
          { "lat": -8.064, "lng": -34.882 },
          { "lat": -8.061, "lng": -34.880 }
        ]
      }
    }
  }
}
```

#### Assinaturas de Usuários (`/subscriptions/{userId}`)
```json
{
  "subscriptions": {
    "123456789": {
      "bikes": ["intenso", "rapida"],
      "stations": ["base01"],
      "system": false
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

### 🔄 Fluxo de Funcionamento

1. **Saída da Base**: Bike perde contato BLE → Bot inicia nova viagem
2. **Coleta em Movimento**: Scans WiFi são processados em tempo real
3. **Geolocalização**: Cada scan é convertido em coordenadas via Google API
4. **Notificações**: Usuários assinantes recebem updates personalizados
5. **Canal Público**: Atividades são publicadas automaticamente
6. **Chegada na Base**: Bike reconecta BLE → Viagem é finalizada
7. **Cálculos**: Distância, CO₂ e métricas são calculadas
8. **Armazenamento**: Viagem completa é salva no Firebase

### 📱 Sistema de Notificações

#### Para Usuários Assinantes
- 🚀 **Saída**: "Sua bike saiu da estação"
- 📍 **Movimento**: Updates de localização (a cada 5 min)
- 🏠 **Chegada**: "Viagem concluída: 2.8km, 406g CO₂ economizado"
- 🔋 **Bateria**: Alertas quando < 20%

#### Para Administradores
- 📡 **Scans detalhados**: Redes WiFi e coordenadas
- 🏢 **Status de estações**: Heartbeats e bikes conectadas
- ⚠️ **Alertas críticos**: Falhas de sistema, timeouts
- 📊 **Métricas**: Estatísticas de uso e performance

#### Canal Público (@prarodar_updates)
- 🚀 **Saídas**: "Bike INTENSO saiu da estação"
- 🏠 **Chegadas**: "Viagem concluída: 2.8km percorridos"
- 🚴 **Em movimento**: "Bike INTENSO está rodando agora"
- 📊 **Estatísticas**: Resumos diários do sistema

## 🚀 Funcionalidades Implementadas

### ✅ **Sistema de Assinaturas**
- Seguir bikes específicas, estações ou sistema completo
- Notificações personalizadas por usuário
- Gerenciamento de assinaturas via comandos

### ✅ **Cálculo de Viagens**
- Detecção automática de início/fim de viagem
- Cálculo de distância via fórmula de Haversine
- Métricas de CO₂ economizado (0.145 kg/km)
- Filtragem de viagens muito curtas (< 80m)

### ✅ **Monitor de Estações**
- Verificação de heartbeats a cada 30 minutos
- Alertas de estações offline/online
- Status de bikes disponíveis por estação
- Informações de bateria e último contato

### ✅ **Canal Público**
- Publicações automáticas de atividades
- Updates de bikes em movimento (throttled)
- Estatísticas diárias do sistema
- Alertas públicos de manutenção

## 🔮 Próximas Melhorias

### Curto Prazo
- [ ] **Cache inteligente**: Evitar chamadas desnecessárias à Google API
- [ ] **Relatórios mensais**: Envio automático para assinantes
- [ ] **Mapas interativos**: Links para visualização de rotas
- [ ] **Alertas de manutenção**: Notificações preventivas

### Médio Prazo
- [ ] **Dashboard web integrado**: Interface administrativa completa
- [ ] **API REST**: Endpoints para integração externa
- [ ] **Estatísticas avançadas**: Análises de padrões de uso
- [ ] **Sistema de gamificação**: Ranking de usuários mais ativos

### Longo Prazo
- [ ] **Machine Learning**: Predição de demanda e rotas
- [ ] **App mobile nativo**: Aplicativo dedicado
- [ ] **Integração IoT**: Sensores adicionais (GPS, acelerômetro)
- [ ] **Blockchain**: Sistema de recompensas descentralizado

## 🔒 Segurança e Privacidade

### 🔐 **Proteção de Dados**
- Todas as credenciais em variáveis de ambiente
- Firebase com service account e regras de segurança
- Rate limiting automático do Telegraf
- Logs estruturados sem dados sensíveis

### 👥 **Privacidade de Usuários**
- Assinaturas armazenadas apenas com ID do Telegram
- Localizações precisas apenas para administradores
- Canal público com dados agregados e anonimizados
- Opção de cancelar assinaturas a qualquer momento

### 🛡️ **Controle de Acesso**
- Comandos administrativos restritos por chat ID
- Validação de entrada em todos os comandos
- Throttling de notificações para evitar spam
- Fallbacks seguros em caso de falha de APIs

## 📝 Logs e Monitoramento

### 📈 **Logs Estruturados**
- ✅ Conexões e inicializações bem-sucedidas
- ❌ Erros detalhados com stack traces
- 📱 Comandos de usuários com timestamps
- 🚴 Eventos de bikes (saída, chegada, scans)
- 🏢 Status de estações e heartbeats
- 📡 Chamadas para APIs externas

### 📉 **Métricas de Performance**
- Tempo de resposta da Google Geolocation API
- Taxa de sucesso de conversão WiFi → coordenadas
- Número de assinaturas ativas por tipo
- Frequência de comandos por usuário
- Estatísticas de viagens processadas

### 🚨 **Alertas de Sistema**
- Falhas de conexão com Firebase
- Timeout de APIs externas
- Estações offline por mais de 30 minutos
- Bikes com bateria crítica
- Erros de processamento de viagens

## 📚 Documentação e Recursos

### 📁 **Arquivos de Configuração**
- [.env.example](.env.example) - Variáveis de ambiente
- [package.json](package.json) - Dependências e scripts
- [firebase.json](firebase.json) - Configuração Firebase

### 📜 **Documentação Técnica**
- [📋 Guia de Deploy](docs/DEPLOY.md) - Opções de deployment
- [🔄 Fluxo da Lógica](docs/FLUXO_LOGICA.md) - Funcionamento interno
- [📜 Scripts Disponíveis](scripts/README.md) - Utilitários e testes

### 🔧 **Ferramentas de Desenvolvimento**
- [route-viewer.html](tools/route-viewer.html) - Visualizador de rotas
- [scripts/test/](scripts/test/) - Scripts de teste
- [scripts/webhook/](scripts/webhook/) - Configuração de webhooks

### 🌐 **Links Úteis**
- Canal Público: [@prarodar_updates](https://t.me/prarodar_updates)
- Bot Principal: [@prarodarbot](https://t.me/prarodarbot)
- Repositório: [GitHub](https://github.com/prarodar/bpr-sistema)
- Documentação: [README Principal](../README.md)

## 🤝 Contribuição

### 🛠️ **Como Contribuir**
1. **Fork** o repositório
2. **Clone** localmente: `git clone <repo-url>`
3. **Entre no diretório**: `cd bpr-sistema/bot`
4. **Instale** dependências: `npm install`
5. **Configure** variáveis: `cp .env.example .env`
6. **Teste** configuração: `node scripts/test/check-env.js`
7. **Teste** localmente: `npm run dev`
8. **Crie** branch: `git checkout -b feature/nova-funcionalidade`
9. **Commit** mudanças: `git commit -m "feat: adiciona nova funcionalidade"`
10. **Push** e abra **Pull Request**

### 📋 **Padrões de Código**
- **ESLint + Prettier** para formatação
- **Conventional Commits** para mensagens
- **JSDoc** para documentação de funções
- **Testes unitários** para novas funcionalidades

### 🐛 **Reportar Bugs**
1. Verifique se já foi reportado nas [Issues](../../issues)
2. Inclua informações do ambiente (Node.js, OS, etc.)
3. Descreva passos para reproduzir
4. Anexe logs relevantes (sem credenciais)

## 📄 Licença

**AGPL-3.0 License** - veja o arquivo [LICENSE](../../LICENSE) para detalhes.

### 📞 **Suporte**
- **Issues**: [GitHub Issues](../../issues)
- **Discussões**: [GitHub Discussions](../../discussions)
- **Telegram**: [@prarodarbot](https://t.me/prarodarbot)
- **Email**: contato@prarodar.org