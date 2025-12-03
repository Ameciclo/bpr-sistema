# Implementação de Status Programado

## 📊 Funcionalidade Implementada

Sistema de atualizações automáticas de status da bicicleta a cada intervalo configurável (padrão: 1 hora).

## ⚙️ Configurações Adicionadas

### Arquivo `config.txt`
```bash
# Atualizações programadas de status
STATUS_UPDATE_INTERVAL_MINUTES=60  # Intervalo em minutos (padrão: 1 hora)
STATUS_UPDATE_ENABLED=1            # Ativar atualizações (1=sim, 0=não)
```

### Estrutura Config (config.h)
```cpp
int statusUpdateIntervalMinutes = 60;  // Intervalo para atualizações de status (minutos)
bool statusUpdateEnabled = true;       // Ativar atualizações programadas
```

## 🔄 Lógica de Funcionamento

### 1. Verificação Temporal
```cpp
bool needsScheduledStatusUpdate() {
  if (!config.statusUpdateEnabled) return false;
  
  // Verificar arquivo /last_status_update.txt
  unsigned long minutesSince = (now - lastUpdate) / 60;
  
  if (minutesSince >= config.statusUpdateIntervalMinutes) {
    return true; // Hora de atualizar
  }
  return false;
}
```

### 2. Upload Automático
- **Quando**: A cada X minutos (configurável) + próximo da base
- **Onde**: Firebase `/bikes/{BIKE_ID}/status/{timestamp}.json`
- **Independente**: Funciona mesmo sem dados WiFi coletados

### 3. Estrutura do Status no Firebase
```json
{
  "timestamp": 1760210000,
  "battery": 85.3,
  "uptime": 3600,
  "mode": "normal",
  "dataFiles": 15,
  "freeHeap": 45000,
  "location": "base",
  "ssid": "Ameciclo",
  "rssi": -45
}
```

## 🚨 Cenários de Ativação

### Cenário 1: Status + Dados
```
1. Detecta base (RSSI > -65dBm)
2. Conecta na base
3. Verifica: passou 1h → Envia status
4. Verifica: tem dados → Faz upload normal
```

### Cenário 2: Só Status
```
1. Detecta base (RSSI > -65dBm)
2. Conecta na base
3. Verifica: passou 1h → Envia status
4. Verifica: sem dados → Só status (não upload)
```

### Cenário 3: Status + Bateria Baixa
```
1. Detecta base
2. Conecta na base
3. Envia: Status programado
4. Envia: Alerta bateria baixa
5. Upload: Dados se houver
```

## 📊 Informações Incluídas no Status

### Dados Básicos
- **timestamp**: Horário da atualização
- **battery**: Nível atual da bateria (%)
- **uptime**: Tempo ligado em segundos
- **mode**: Modo de coleta atual

### Dados do Sistema
- **dataFiles**: Quantidade de arquivos coletados
- **freeHeap**: Memória livre disponível
- **location**: "base" ou "mobile"

### Dados de Localização (se na base)
- **ssid**: Nome da base WiFi conectada
- **rssi**: Força do sinal WiFi

## 🛡️ Controle de Frequência

### Arquivo de Controle
- **Arquivo**: `/last_status_update.txt`
- **Conteúdo**: Timestamp da última atualização
- **Verificação**: A cada conexão na base

### Configuração Flexível
- **Intervalo ajustável**: Via `STATUS_UPDATE_INTERVAL_MINUTES`
- **Ativação/desativação**: `STATUS_UPDATE_ENABLED`
- **Primeira execução**: Sempre envia status

## 📈 Benefícios

### Para Monitoramento
- **Status regular**: Saber se bicicleta está funcionando
- **Localização**: Onde foi vista pela última vez
- **Performance**: Memória, uptime, arquivos coletados
- **Bateria**: Histórico de níveis ao longo do tempo

### Para Manutenção
- **Detecção de problemas**: Bicicletas que pararam de reportar
- **Análise de uso**: Padrões de coleta e movimento
- **Capacidade**: Monitorar espaço de armazenamento
- **Conectividade**: Histórico de bases utilizadas

## 🔧 Integração com Sistema Existente

### Prioridade de Upload
1. **Alerta bateria baixa** (crítico)
2. **Status programado** (regular)
3. **Dados WiFi** (normal)

### Aproveitamento de Conexões
- **Eficiência**: Usa conexões existentes na base
- **Batching**: Envia tudo numa única conexão
- **Fallback**: Funciona mesmo sem dados WiFi

## 🚀 Exemplo de Funcionamento

```
📊 STATUS UPDATE: 65 min desde última atualização
🏠 PRÓXIMO À BASE - Tentando conectar...
🌐 CONECTADO À BASE!
📈 Enviando status programado: 78.5% bateria
✅ Status programado enviado!
⬆️ Fazendo upload da sessão completa...
```

## 📊 Estrutura Firebase Completa

```json
{
  "bikes": {
    "teste6": {
      "status": {
        "1760210000": {
          "timestamp": 1760210000,
          "battery": 78.5,
          "uptime": 7200,
          "mode": "normal",
          "dataFiles": 12,
          "location": "base",
          "ssid": "Ameciclo"
        }
      },
      "alerts": {
        "1760209500": {
          "type": "low_battery",
          "level": 12.3
        }
      },
      "sessions": {
        "20241201_001": {
          "scans": [...]
        }
      }
    }
  }
}
```

## 🎯 Configurações Recomendadas

### Para Monitoramento Normal
```bash
STATUS_UPDATE_INTERVAL_MINUTES=60  # 1 hora
STATUS_UPDATE_ENABLED=1
```

### Para Monitoramento Intensivo
```bash
STATUS_UPDATE_INTERVAL_MINUTES=30  # 30 minutos
STATUS_UPDATE_ENABLED=1
```

### Para Economizar Bateria
```bash
STATUS_UPDATE_INTERVAL_MINUTES=120 # 2 horas
STATUS_UPDATE_ENABLED=1
```

## ✅ Resultado Final

Sistema completo de monitoramento que:
- ✅ **Atualiza** status automaticamente a cada hora
- ✅ **Monitora** bateria, memória e performance
- ✅ **Localiza** bicicletas via bases WiFi
- ✅ **Integra** com alertas e coleta de dados
- ✅ **Configura** intervalos via arquivo
- ✅ **Otimiza** uso de bateria e conectividade

A implementação está completa e pronta para monitoramento contínuo da frota!