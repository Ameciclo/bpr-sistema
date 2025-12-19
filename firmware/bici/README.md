# 🚲 BPR Bici Firmware

Firmware para bicicletas ESP32 compatível com o sistema BPR Central.

## 🔄 Estados da Máquina

- **BOOT**: Inicialização e verificação de bateria
- **CONFIG_REQUEST**: Primeira conexão, solicita configuração da central
- **SCANNING**: Coleta dados WiFi e procura central
- **AT_BASE**: Conectado à central, sincronizando dados
- **SLEEP**: Modo de economia de energia

## 🔵 Protocolo BLE

### Descoberta da Central
- Escaneia por dispositivo BLE com nome: `BPR Central`
- Service UUID: `12345678-1234-1234-1234-123456789abc`

### Características BLE

#### Data Characteristic (Envio de Dados)
- **UUID**: `87654321-4321-4321-4321-cba987654321`
- **Uso**: Enviar status e dados WiFi para central

**Formato Status:**
```json
{
  "bike_id": "bpr-abc123",
  "battery": 3.8,
  "records": 15,
  "timestamp": 12345,
  "heap": 45000
}
```

**Formato WiFi Data:**
```json
{
  "bike_id": "bpr-abc123",
  "battery": 3.8,
  "records": 15,
  "timestamp": 12345,
  "wifi_scans": [
    {
      "ssid": "NET_5G_HOME",
      "bssid": "AA:BB:CC:DD:EE:FF",
      "rssi": -65,
      "channel": 6
    }
  ]
}
```

#### Config Characteristic (Configurações)
- **UUID**: `11111111-2222-3333-4444-555555555555`
- **Uso**: Receber configurações da central via notificações

**Formato Config Request:**
```json
{
  "type": "config_request",
  "bike_id": "bpr-abc123"
}
```

**Formato Config Response (via notificação):**
```json
{
  "target_bike": "bpr-abc123",
  "timestamp": 12345,
  "config": {
    "bike_name": "Bike Intenso",
    "version": 2,
    "wifi": {
      "scan_interval_sec": 300,
      "scan_timeout_ms": 5000,
      "max_networks": 20,
      "rssi_threshold": -90
    },
    "ble": {
      "base_name": "BPR Central",
      "scan_time_sec": 5
    },
    "power": {
      "deep_sleep_duration_sec": 3600,
      "radio_coordination_delay_ms": 300
    },
    "battery": {
      "critical_voltage": 3.2,
      "low_voltage": 3.45
    }
  }
}
```

## 🔧 Configuração

### ID Único
- Gerado automaticamente baseado no chip ID: `bpr-XXXXXX`
- Armazenado em `/config.json` no LittleFS

### Configuração Dinâmica
1. **Primeira inicialização**: Estado CONFIG_REQUEST
2. **Conecta na central**: Solicita configuração via BLE
3. **Recebe config**: Atualiza parâmetros e salva
4. **Funcionamento normal**: Usa configurações recebidas

### Configurações Disponíveis
- **WiFi**: Intervalos de scan, timeout, filtros RSSI
- **BLE**: Nome da central, timeouts de conexão
- **Power**: Delays de coordenação, deep sleep
- **Battery**: Thresholds de voltagem crítica/baixa

## 🔋 Gerenciamento de Energia

### Modos de Operação
- **Normal**: Scan WiFi a cada 5 minutos
- **Bateria baixa**: Scan WiFi a cada 15 minutos
- **Bateria crítica**: Deep sleep por 1 hora

### Coordenação de Rádios
- Delay de 300ms entre WiFi scan e BLE scan
- Evita interferência entre os rádios

## 📡 Coleta de Dados WiFi

### Buffer Local
- Armazena até 100 registros WiFi
- Persiste no LittleFS durante reboots
- Limpa após envio bem-sucedido

### Filtros
- RSSI mínimo: -90 dBm
- Máximo 20 redes por scan
- Timeout de 5 segundos por scan

## 🏠 Sincronização com Central

### Fluxo de Dados
1. **Detecta central**: Via scan BLE
2. **Conecta**: Estabelece conexão BLE
3. **Envia status**: Dados básicos da bike
4. **Envia WiFi**: Buffer de scans coletados
5. **Recebe config**: Atualizações via notificação
6. **Desconecta**: Quando central entra em modo WiFi

### Reconexão Automática
- Tenta reconectar se perde conexão
- Fallback para modo SCANNING se falha

## 🛠️ Desenvolvimento

### Compilação
```bash
cd firmware/bici
pio run --target upload
pio run --target uploadfs  # Upload config.json
```

### Debug
- Serial 115200 baud
- Logs detalhados de cada estado
- Modo dev_mode ignora bateria baixa

### Testes
- Use alimentação USB para desenvolvimento
- Botão FLASH para forçar config padrão
- LED indica status da conexão

## 🔍 Troubleshooting

### Bike não conecta na central
- Verificar nome BLE da central
- Confirmar UUIDs das características
- Central pode estar em modo WiFi sync

### Dados não chegam no Firebase
- Bike pode estar PENDING (não aprovada)
- Verificar formato JSON dos dados
- Confirmar campo bike_id obrigatório

### Bateria sempre crítica
- Verificar conexão do pino BATTERY_PIN
- Usar dev_mode=true para testes
- Alimentação USB simula 4.0V