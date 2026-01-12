# 🌐 BPR Sistema - Dashboard Web

Dashboard web moderno para monitoramento do sistema de bicicletas BPR.

## 🚀 Tecnologias

- **Framework**: [Remix](https://remix.run/) v2.17.2
- **Build Tool**: [Vite](https://vitejs.dev/) v6.4.1
- **Frontend**: React 18.3.1
- **Licença**: AGPL-3.0

## 📁 Estrutura

```
web/
├── app/
│   └── root.jsx          # 🚀 Componente raiz da aplicação
├── package.json          # 📦 Dependências e scripts
├── vite.config.js        # ⚙️ Configuração do Vite
└── README.md            # 📚 Esta documentação
```

## 🛠️ Desenvolvimento

### Pré-requisitos
- Node.js 18+
- npm ou yarn

### Setup Local
```bash
# Navegar para o diretório web
cd web

# Instalar dependências
npm install

# Iniciar servidor de desenvolvimento
npm run dev

# Acessar: http://localhost:5173
```

### Scripts Disponíveis
```bash
npm run dev      # Servidor de desenvolvimento com hot reload
npm run build    # Build para produção
npm run start    # Servidor de produção
npm test         # Executar testes (não implementado)
```

## 🔧 Configuração

### Integração Firebase
O dashboard se conecta ao Firebase Realtime Database para:
- Monitorar status das bicicletas em tempo real
- Visualizar dados de sessões e scans WiFi
- Gerenciar configurações do sistema
- Exibir métricas e estatísticas

### Estrutura de Dados
Consome os seguintes endpoints do Firebase:
- `/bikes/{bikeId}` - Status e dados das bicicletas
- `/bases/{baseId}` - Status das centrais/estações
- `/rides/{bikeId}` - Histórico de viagens
- `/public_stats` - Estatísticas públicas do sistema

## 🎯 Funcionalidades Planejadas

### 📊 Dashboard Principal
- [ ] **Visão geral**: Status de todas as bicicletas
- [ ] **Mapa interativo**: Localização em tempo real
- [ ] **Métricas**: KPIs do sistema (km total, CO₂ economizado)
- [ ] **Alertas**: Notificações de bateria baixa e falhas

### 🚲 Gestão de Bicicletas
- [ ] **Lista de bikes**: Status, bateria, última localização
- [ ] **Histórico**: Viagens e rotas percorridas
- [ ] **Configurações**: Parâmetros por bicicleta
- [ ] **Manutenção**: Log de eventos e problemas

### 🏢 Gestão de Estações
- [ ] **Status das centrais**: Online/offline, heartbeat
- [ ] **Configurações**: Parâmetros por estação
- [ ] **Bikes conectadas**: Quantas bikes por estação
- [ ] **Logs**: Histórico de conexões e eventos

### 📈 Relatórios e Analytics
- [ ] **Relatórios mensais**: Uso, distâncias, impacto ambiental
- [ ] **Gráficos**: Tendências de uso ao longo do tempo
- [ ] **Exportação**: Dados em CSV/PDF
- [ ] **Comparativos**: Performance entre bikes/períodos

### ⚙️ Administração
- [ ] **Configurações globais**: Parâmetros do sistema
- [ ] **Gestão de usuários**: Permissões e acessos
- [ ] **Backup/Restore**: Dados e configurações
- [ ] **Logs do sistema**: Debug e monitoramento

## 🚀 Deploy

### Build de Produção
```bash
# Gerar build otimizado
npm run build

# Testar build localmente
npm run start
```

### Opções de Deploy
- **Vercel**: Deploy automático via Git
- **Netlify**: Integração contínua
- **Firebase Hosting**: Integração nativa
- **Servidor próprio**: Via Docker ou PM2

### Variáveis de Ambiente
```bash
# Firebase (produção)
FIREBASE_PROJECT_ID=seu_projeto
FIREBASE_API_KEY=sua_chave
FIREBASE_DATABASE_URL=https://projeto.firebaseio.com

# Configurações opcionais
NODE_ENV=production
PORT=3000
```

## 🔗 Integração com Outros Componentes

### 🤖 Bot Telegram
- Compartilha dados do Firebase
- Links para visualização de rotas
- Notificações de eventos críticos

### 🚲 Firmware
- Recebe dados via Firebase
- Monitora status das bikes
- Configura parâmetros remotamente

### 📊 Firebase
- Fonte única de dados
- Sincronização em tempo real
- Estrutura de dados padronizada

## 🛠️ Desenvolvimento Futuro

### Próximas Implementações
1. **Componentes base**: Layout, navegação, autenticação
2. **Integração Firebase**: Hooks e providers React
3. **Dashboard principal**: Visão geral do sistema
4. **Mapas**: Integração com Leaflet/Google Maps
5. **Relatórios**: Gráficos com Chart.js/Recharts

### Arquitetura Planejada
```
app/
├── routes/                 # Rotas do Remix
│   ├── _index.tsx         # Dashboard principal
│   ├── bikes/             # Gestão de bicicletas
│   ├── stations/          # Gestão de estações
│   └── reports/           # Relatórios
├── components/            # Componentes reutilizáveis
│   ├── ui/               # Componentes base
│   ├── charts/           # Gráficos e visualizações
│   └── maps/             # Componentes de mapa
├── services/             # Integração Firebase
├── utils/                # Utilitários
└── styles/               # CSS/Tailwind
```

## 📚 Recursos

- [Remix Documentation](https://remix.run/docs)
- [Vite Guide](https://vitejs.dev/guide/)
- [React Documentation](https://react.dev/)
- [Firebase Web SDK](https://firebase.google.com/docs/web/setup)

## 🤝 Contribuição

Veja o [README principal](../README.md) para diretrizes de contribuição.

## 📄 Licença

AGPL-3.0 - veja [LICENSE](LICENSE) para detalhes.