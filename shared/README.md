# Shared Resources

Recursos compartilhados entre todos os componentes do sistema BPR.

## Conteúdo

### 📋 Configurações
- Configurações Firebase
- Constantes do sistema
- Schemas de dados

### 🔧 Utilitários
- Funções de validação
- Helpers de formatação
- Tipos TypeScript

### 📊 Schemas
- Estrutura de dados Firebase
- Validações JSON Schema
- Tipos de dados compartilhados

## Estrutura

```
shared/
├── config/           # Configurações globais
├── types/            # Tipos TypeScript
├── utils/            # Utilitários
├── schemas/          # Schemas de dados
└── constants/        # Constantes
```

## Uso

Cada componente pode importar recursos compartilhados:

```typescript
// No bot
import { BikeStatus } from '../shared/types/bike'

// No web
import { formatBatteryLevel } from '../shared/utils/formatters'

// No firmware (quando aplicável)
// Configurações podem ser copiadas
```

## Vantagens

- ✅ Consistência entre componentes
- ✅ Reutilização de código
- ✅ Manutenção centralizada
- ✅ Tipagem compartilhada