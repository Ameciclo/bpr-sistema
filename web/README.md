# BPR Web Dashboard (botaprarodar)

Interface web em Remix para monitoramento e gestão das bicicletas.

## Funcionalidades (Planejadas)

- 📊 Dashboard em tempo real
- 🗺️ Mapa com localização das bicicletas
- 📈 Gráficos e relatórios
- ⚙️ Configuração do sistema
- 👥 Gestão de usuários
- 📱 Interface responsiva

## Tecnologias

- Remix
- React
- TypeScript
- Tailwind CSS
- Firebase SDK
- Mapbox/Leaflet

## Configuração

```bash
npm install
cp .env.example .env
# Configure as variáveis no .env
npm run dev
```

## Páginas Planejadas

- `/` - Dashboard principal
- `/bikes` - Lista de bicicletas
- `/bike/:id` - Detalhes da bicicleta
- `/map` - Mapa em tempo real
- `/reports` - Relatórios
- `/settings` - Configurações

## Status

🚧 **Em desenvolvimento** - Interface será criada após estabilização do backend.

## Estrutura

```
web/
├── app/
│   ├── routes/       # Rotas do Remix
│   ├── components/   # Componentes React
│   ├── services/     # Integração Firebase
│   └── utils/        # Utilitários
├── public/           # Assets estáticos
└── package.json
```

## Deploy

- Vercel (recomendado)
- Netlify
- Railway