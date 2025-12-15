# 🚲 BPR Bici - Firmware v2.0

Firmware melhorado para bicicletas do sistema BPR com máquina de estados otimizada e comunicação BLE com a base.

## 🎯 Características Principais

- **Máquina de Estados**: BOOT → AT_BASE ↔ SCANNING → LOW_POWER → DEEP_SLEEP
- **Comunicação BLE**: Conecta automaticamente com bases "BPR*"
- **Scanner WiFi**: Coleta redes para geolocalização offline
- **Gerenciamento de Energia**: Coordenação de rádio WiFi/BLE e modos de economia
- **Configuração Dinâmica**: Recebe configurações da base via BLE
- **Persistência**: Salva dados em caso de deep sleep

## 📁 Estrutura Modular

```
src/
├── main.cpp              # 🚀 Máquina de estados principal
├── config_manager.cpp    # ⚙️ Configurações dinâmicas via BLE
├── battery_monitor.cpp   # 🔋 Monitor de bateria com ADC
├── ble_client.cpp        # 🔵 Cliente BLE para comunicação
├── wifi_scanner.cpp      # 📡 Scanner WiFi com buffer local
├── power_manager.cpp     # ⚡ Gerenciamento de energia
├── at_base.cpp          # 🏠 Estado: conectado à base
├── scanning.cpp         # 📡 Estado: coletando dados
├── low_power.cpp        # ⚡ Estado: economia de energia
└── deep_sleep.cpp       # 💤 Estado: hibernação profunda
```

## 🔄 Fluxo de Estados

### 1️⃣ BOOT
- Inicializa hardware e módulos
- Carrega configuração local
- Verifica bateria
- Procura base BLE
- **Transições**: → AT_BASE (base encontrada) | → SCANNING (sem base)

### 2️⃣ AT_BASE
- Conecta via BLE à base
- Envia status da bicicleta
- Recebe configurações atualizadas
- Envia dados WiFi coletados
- **Transições**: → SCANNING (conexão perdida)

### 3️⃣ SCANNING
- Executa scans WiFi periódicos
- Procura base BLE a cada 10s
- Coordena uso de rádio (WiFi → delay 300ms → BLE)
- **Transições**: → AT_BASE (base encontrada) | → LOW_POWER (bateria baixa/tempo)

### 4️⃣ LOW_POWER
- Reduz frequência de scans (15min)
- Diminui potência de transmissão
- Continua procurando base
- **Transições**: → AT_BASE (base encontrada) | → DEEP_SLEEP (bateria crítica) | → SCANNING (bateria recuperada)

### 5️⃣ DEEP_SLEEP
- Salva dados em LittleFS
- Desabilita periféricos
- Hibernação profunda (1h ou botão)
- **Transições**: → BOOT (wake-up)

## ⚙️ Configuração

### Hardware (constants.h)
```cpp
#define LED_PIN 8              // LED de status
#define BUTTON_PIN 9           // Botão de emergência
#define BATTERY_PIN A0         // Monitor de bateria
```

### Configuração Dinâmica (via BLE)
```json
{
  "bike_id": "bici_001",
  "base_ble_name": "BPR",
  "scan_interval_sec": 300,
  "scan_interval_low_batt_sec": 900,
  "deep_sleep_sec": 3600,
  "min_battery_voltage": 3.45,
  "version": 1,
  "timestamp": 1234567890
}
```

## 🔋 Gerenciamento de Energia

### Consumo Estimado
- **AT_BASE**: ~5mA (BLE ativo, light sleep)
- **SCANNING**: ~50mA (WiFi + BLE scans)
- **LOW_POWER**: ~2mA (scans reduzidos)
- **DEEP_SLEEP**: ~10µA (hibernação)

### Coordenação de Rádio
- **Problema**: ESP32-C3 pode ter interferência WiFi/BLE simultâneo
- **Solução**: Delay de 300ms entre WiFi scan e BLE scan
- **Benefício**: Evita conflitos mantendo ambas funcionalidades

## 🔵 Comunicação BLE

### Descoberta de Base
- Scan por dispositivos com nome iniciado em "BPR"
- Conexão automática quando encontrada
- Timeout de 10s para conexão

### Características BLE
- **Status**: Envia dados da bicicleta (bateria, registros, etc.)
- **Config**: Recebe configurações da base
- **Data**: Envia dados WiFi coletados em lotes

### Protocolo de Dados
```json
// Status da Bicicleta
{
  "type": "bike_status",
  "bike_id": "bici_001",
  "battery_voltage": 3.82,
  "battery_percentage": 85,
  "records_count": 42,
  "timestamp": 1234567890,
  "heap": 174248
}

// Dados WiFi
{
  "scans": [
    {
      "ts": 1234567890,
      "bssid": "AA:BB:CC:DD:EE:FF",
      "rssi": -70,
      "ch": 6
    }
  ]
}
```

## 📡 Scanner WiFi

### Funcionamento
- Scan periódico baseado na configuração
- Filtra sinais fracos (RSSI > -90dBm)
- Buffer local de até 100 registros
- Persistência em SPIFFS para deep sleep

### Otimizações
- Timeout de 5s por scan
- Máximo 20 redes por scan
- Conversão BSSID para string otimizada
- Remoção automática de registros antigos

## 🚨 Modo Emergência

### Ativação
- Pressionar botão BOOT por 3 segundos
- LED pisca rapidamente
- Sistema pausa operação

### Comandos
- **'r'**: Reinicia o sistema
- **'c'**: Continua operação normal

## 🔧 Build e Upload

### Pré-requisitos
```bash
# PlatformIO CLI
pip install platformio

# Dependências
pio lib install "ArduinoJson@^7.0.0"
pio lib install "ESP32 BLE Arduino@^2.0.0"
```

### Compilação
```bash
cd firmware/bici
pio run                    # Compilar
pio run --target upload    # Upload firmware
pio run --target uploadfs  # Upload filesystem (LittleFS)
```

### Monitor Serial
```bash
pio device monitor --baud 115200
```

## 📊 Monitoramento

### Status Periódico (30s)
```
==================================================
🚲 bici_001 | Estado: SCANNING | Uptime: 1234s
🔋 3.82V (85%) ✅ | 📡 42 registros
🔵 BLE: Desconectado | ⏱️ Estado há: 120s
==================================================
```

### Indicadores LED
- **BOOT**: 3 piscadas rápidas
- **AT_BASE**: LED fixo
- **SCANNING**: Piscada a cada scan
- **LOW_POWER**: Piscada lenta
- **DEEP_SLEEP**: LED desligado
- **EMERGÊNCIA**: Piscadas muito rápidas

## 🔍 Debug e Logs

### Níveis de Log
```cpp
#define CORE_DEBUG_LEVEL 3  // 0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose
```

### Logs Importantes
- ✅ Inicialização de módulos
- 🔍 Descoberta e conexão BLE
- 📡 Resultados de scans WiFi
- 🔋 Status de bateria
- ⚡ Mudanças de estado
- 💾 Operações de persistência

## 🚀 Melhorias Implementadas

### vs firmware/bike
1. **Máquina de Estados Clara**: Cada estado em arquivo separado
2. **Configuração Dinâmica**: Recebe config da base via BLE
3. **Coordenação de Rádio**: Evita interferência WiFi/BLE
4. **Persistência Melhorada**: Salva dados antes de deep sleep
5. **Gerenciamento de Energia**: Modos otimizados por situação
6. **Código Modular**: Separação clara de responsabilidades
7. **Modo Emergência**: Debug e controle via botão
8. **Logs Estruturados**: Melhor rastreamento de problemas

## 📈 Próximos Passos

- [ ] Integração com base/hub BLE
- [ ] Testes de autonomia de bateria
- [ ] Otimização de consumo energético
- [ ] Implementação de watchdog
- [ ] Compressão de dados WiFi
- [ ] Criptografia BLE (opcional)
- [ ] OTA updates via BLE
- [ ] Métricas de performance