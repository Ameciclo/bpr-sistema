# Implementação de Alerta de Bateria Baixa

## 🔋 Funcionalidade Implementada

Sistema de alerta automático quando a bateria da bicicleta está baixa e próxima de uma base WiFi.

## ⚙️ Configurações Adicionadas

### Arquivo `config.txt`
```bash
# Alerta de bateria baixa
BATTERY_LOW_THRESHOLD=15.0      # Threshold para bateria baixa (%)
BATTERY_CRITICAL_THRESHOLD=5.0  # Threshold para bateria crítica (%)
LOW_BATTERY_ALERT_ENABLED=1     # Ativar alertas (1=sim, 0=não)
```

### Estrutura Config (config.h)
```cpp
float batteryLowThreshold = 15.0;        // Threshold para bateria baixa (%)
float batteryCriticalThreshold = 5.0;    // Threshold para bateria crítica (%)
bool lowBatteryAlertEnabled = true;      // Ativar alertas de bateria baixa
```

## 🔄 Lógica de Funcionamento

### 1. Verificação Inteligente
```cpp
bool needsLowBatteryAlert() {
  if (!config.lowBatteryAlertEnabled) return false;
  
  float batteryLevel = getBatteryLevel();
  
  // Verificar se bateria está baixa
  if (batteryLevel <= config.batteryLowThreshold) {
    // Controle anti-spam: só alerta a cada 30 minutos
    // Verificar arquivo /last_battery_alert.txt
    return true;
  }
  return false;
}
```

### 2. Upload Automático
- **Quando**: Detecta base + bateria baixa
- **Onde**: Firebase `/bikes/{BIKE_ID}/alerts/{timestamp}.json`
- **Frequência**: Máximo 1 alerta a cada 30 minutos

### 3. Estrutura do Alerta no Firebase
```json
{
  "type": "low_battery",
  "level": 12.3,
  "critical": false,
  "threshold": 15.0,
  "base": "Ameciclo",
  "timestamp": 1760210000,
  "ip": "192.168.1.100"
}
```

## 🚨 Cenários de Ativação

### Cenário 1: Bateria Baixa com Dados
```
1. Detecta base (RSSI > -65dBm)
2. Conecta na base
3. Verifica: bateria < 15% → Envia alerta
4. Verifica: tem dados → Faz upload normal
```

### Cenário 2: Bateria Baixa sem Dados
```
1. Detecta base (RSSI > -65dBm)
2. Conecta na base
3. Verifica: bateria < 15% → Envia alerta
4. Verifica: sem dados → Só alerta (não upload)
```

### Cenário 3: Bateria Crítica
```
1. Detecta base
2. Conecta na base
3. Bateria < 5% → Alerta marcado como "critical": true
4. Upload prioritário do alerta
```

## 🛡️ Proteções Implementadas

### Anti-Spam
- **Arquivo controle**: `/last_battery_alert.txt`
- **Intervalo mínimo**: 30 minutos entre alertas
- **Verificação**: Timestamp do último alerta enviado

### Configuração Flexível
- **Threshold ajustável**: Via config.txt
- **Ativação/desativação**: `LOW_BATTERY_ALERT_ENABLED`
- **Níveis**: Baixa (15%) e Crítica (5%)

## 📊 Benefícios

### Para Operação
- **Monitoramento proativo**: Saber quando bicicleta precisa carga
- **Localização**: Onde está quando bateria baixa
- **Histórico**: Padrões de uso da bateria
- **Manutenção**: Alertas para equipe técnica

### Para Sistema
- **Baixo impacto**: Só conecta quando necessário
- **Eficiência**: Aproveita conexões existentes
- **Robustez**: Funciona mesmo sem dados WiFi
- **Flexibilidade**: Configuração via arquivo

## 🔧 Arquivos Modificados

1. **config.h** - Estrutura de configuração
2. **config.cpp** - Carregamento/salvamento das configurações
3. **wifi_scanner.h/cpp** - Função de verificação de alerta
4. **firebase.h/cpp** - Função de upload de alerta
5. **main.cpp** - Integração na lógica principal
6. **config.txt** - Configurações de exemplo

## 🚀 Como Usar

### 1. Configurar Thresholds
```bash
# Editar data/config.txt
BATTERY_LOW_THRESHOLD=20.0     # Alerta aos 20%
BATTERY_CRITICAL_THRESHOLD=10.0 # Crítico aos 10%
LOW_BATTERY_ALERT_ENABLED=1    # Ativar alertas
```

### 2. Upload e Teste
```bash
pio run --target uploadfs  # Upload configurações
pio run --target upload    # Upload código
```

### 3. Monitoramento
- **Serial**: Mensagens de debug dos alertas
- **Firebase**: Consultar `/bikes/{BIKE_ID}/alerts/`
- **Logs**: Arquivo `/last_battery_alert.txt`

## 📈 Exemplo de Funcionamento

```
🔋 Bateria: 3.650V (12.5%) - ADC: 1825mV - Cal: 1.025
🚨 BATERIA BAIXA: 12.5% (threshold: 15.0%)
🏠 PRÓXIMO À BASE - Tentando conectar...
🌐 CONECTADO À BASE!
🚨 Enviando alerta de bateria baixa...
✅ Alerta de bateria enviado!
```

## 🎯 Resultado Final

Sistema inteligente que:
- ✅ **Detecta** bateria baixa automaticamente
- ✅ **Conecta** na base quando necessário
- ✅ **Envia** alerta para Firebase
- ✅ **Evita** spam de alertas
- ✅ **Funciona** independente de dados WiFi
- ✅ **Configura** via arquivo de texto

A implementação está completa e pronta para uso!