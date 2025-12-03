# Sistema de Monitoramento de Bateria

## Hardware Implementado

### Divisor de Tensão
- **Pino**: A0 do XIAO ESP32-C3
- **Resistores**: 2x 220kΩ em série
- **Configuração**: 
  - Bateria+ → R1 (220kΩ) → A0 → R2 (220kΩ) → Bateria-
  - Divisão: 1:1 (tensão dividida por 2)

### Especificações
- **Tensão máxima de entrada**: 8.4V (2x bateria Li-ion)
- **Tensão no ADC**: 0-4.2V (limitado a 3.3V pelo ESP32)
- **Resolução ADC**: 12 bits (0-4095)
- **Atenuação**: 11dB (permite leitura até 3.3V)

## Software

### Função `getBatteryLevel()`
```cpp
float getBatteryLevel() {
  uint32_t totalVoltage = 0;
  for(int i = 0; i < 16; i++) {
    totalVoltage += analogReadMilliVolts(A0); // ADC com correção
  }
  float avgVoltage = totalVoltage / 16.0;         // Média em mV
  float batteryVoltage = (avgVoltage * 2.0) / 1000.0; // Compensa divisor
  float percentage = ((batteryVoltage - 3.0) / (4.2 - 3.0)) * 100.0;
  return constrain(percentage, 0, 100);
}
```

### Configuração ADC
```cpp
pinMode(A0, INPUT);  // Configurar pino A0 como entrada
// analogReadMilliVolts() usa correção automática do chip
```

## Calibração

### Tensões de Referência (Li-ion)
- **4.2V** = 100% (bateria carregada)
- **3.7V** = 50% (tensão nominal)
- **3.0V** = 0% (bateria descarregada)

### Valores ADC Esperados
| Bateria | ADC | Tensão D0 | Porcentagem |
|---------|-----|-----------|-------------|
| 4.2V    | 2600| 2.1V      | 100%        |
| 3.7V    | 2290| 1.85V     | 58%         |
| 3.0V    | 1860| 1.5V      | 0%          |

## Debug e Monitoramento

### Logs Automáticos
- Debug a cada 10 segundos no Serial
- Formato: `🔋 ADC: 2600, Tensão: 2.10V, Bateria: 4.20V, Nível: 100.0%`

### Tracking no Firebase
- Histórico de bateria enviado automaticamente
- Mudanças > 2% ou a cada 5 minutos
- Formato: `{"time": timestamp, "level": percentage}`

## Troubleshooting

### Problemas Comuns

1. **Bateria sempre 0%**:
   - Verificar conexões do divisor de tensão
   - Medir tensão no pino A0 com multímetro
   - Deve ser metade da tensão da bateria
   - Verificar se bateria está realmente conectada

2. **Leitura instável**:
   - Adicionar capacitor de 100nF no pino A0
   - Verificar soldas dos resistores
   - Média de 16 leituras já implementada para remover spikes

3. **Valores incorretos**:
   - Verificar se resistores são realmente 220kΩ
   - Calibrar tensões de referência se necessário
   - Medir tensão real da bateria
   - XIAO ESP32-C3 usa correção automática, mas pode variar ±10%

### Comandos de Teste

```bash
# Monitor serial para ver debug
pio device monitor --baud 115200

# Procurar logs de bateria
grep "ADC:" monitor.log
```

### Calibração Manual

Se necessário ajustar as tensões de referência:

```cpp
// Em getBatteryLevel(), modificar:
float percentage = ((batteryVoltage - 3.2) / (4.1 - 3.2)) * 100.0;
//                                    ^^^    ^^^
//                                   Min    Max
```

## Melhorias Futuras

### Hardware
- [ ] Capacitor de filtro no ADC
- [ ] Proteção contra sobretensão
- [ ] Detecção de carregamento (pino adicional)

### Software
- [ ] Média móvel das leituras
- [ ] Calibração automática
- [ ] Alertas de bateria baixa
- [ ] Modo de economia extrema < 10%

## Consumo de Energia

### Medições Típicas
- **WiFi scan**: ~80mA por 3-5s
- **Standby**: ~20mA
- **Deep sleep**: ~10µA (não implementado)

### Autonomia Estimada
- **Bateria 2000mAh**: ~24-48h (dependendo da frequência de scan)
- **Scan a cada 30s**: ~24h
- **Scan a cada 5min**: ~48h