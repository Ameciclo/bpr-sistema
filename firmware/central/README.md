# Central Base Station Firmware

Firmware para o módulo Central da Base do sistema Bota Pra Rodar (BPR), desenvolvido para o Seeed Studio XIAO ESP32-C3.

## 🎯 Funcionalidades

### Operação por Modos
- ✅ **Modo Setup AP**: Configuração inicial via interface web
- ✅ **Modo BLE**: Operação padrão com baixo consumo
- ✅ **Modo WiFi**: Ativação automática para sincronização
- ✅ **Modo Shutdown**: Desligamento controlado do WiFi

### Sistema de LED Inteligente (ESP32C3 SuperMini)
- ✅ **Inicializando**: Piscar rápido (100ms)
- ✅ **Modo Setup**: Piscar alternado (1s) - primeira configuração
- ✅ **BLE Ativo**: Piscar lento (2s) - funcionamento normal
- ✅ **Bike Chegou**: 3 piscadas rápidas - nova bike conectada
- ✅ **Bike Saiu**: 1 piscada longa - bike desconectada
- ✅ **Contagem**: N piscadas = N bikes conectadas (a cada 30s)
- ✅ **Sincronizando**: Piscar médio (500ms) - upload Firebase
- ✅ **Erro**: Piscar muito rápido (50ms) - falha crítica

### Comunicação e Sincronização
- ✅ Conexão WiFi sob demanda (não permanente)
- ✅ Sincronização NTP automática com correção de timestamps
- ✅ **Download de configurações do Firebase**
- ✅ Upload direto para Firebase via HTTPS
- ✅ Sistema de batches para grandes volumes de dados
- ✅ **Cache local de configurações com validade**

### Bluetooth Low Energy (BLE)
- ✅ Servidor BLE sempre ativo
- ✅ **Suporte a até 10 conexões simultâneas**
- ✅ **Gerenciamento inteligente de múltiplas bicicletas**
- ✅ **Identificação e rastreamento individual de cada bike**
- ✅ Recepção de dados das bicicletas (status, WiFi scans, alertas)
- ✅ **Envio automático de configurações para bikes**
- ✅ Correção automática de timestamps das bicicletas
- ✅ Detecção de alertas críticos com sync forçada

### Resiliência e Eficiência
- ✅ Buffer em memória para dados offline
- ✅ Divisão automática em batches (limite 8KB)
- ✅ Correção temporal para bicicletas sem NTP
- ✅ Monitoramento contínuo de heap e conexões

## 🏗️ Arquitetura

O firmware utiliza uma arquitetura modular baseada em máquina de estados:

### **Modos de Operação**
1. **MODE_BLE_ONLY** - Operação padrão, BLE ativo, WiFi desligado
2. **MODE_WIFI_SYNC** - WiFi temporário para sincronização com Firebase
3. **MODE_SHUTDOWN** - Desligamento controlado do WiFi

### **Módulos Principais**
- **Config Manager** - Download e cache de configurações
- **Bike Manager** - Gerenciamento de múltiplas bicicletas
- **BLE Simple** - Comunicação Bluetooth
- **Main Loop** - Coordenação dos modos

## 📡 Serviços BLE

### Serviço Principal (UUID: "BAAD")
- **Bike ID Characteristic** (UUID: "F00D") - Identificação e dados da bicicleta
- **Battery Characteristic** (UUID: "BEEF") - Status de bateria e alertas

### Características Suportadas
- **Read/Write** - Recepção de dados das bicicletas
- **Notify** - Notificações de status
- **Callbacks** - Processamento automático de dados recebidos

### **Gerenciamento de Múltiplas Bikes**
- **Identificação Automática** - Cada bike é identificada pelo UID
- **Rastreamento Individual** - Handle de conexão, último contato, bateria
- **Configuração Personalizada** - Envio de config específica por bike
- **Limpeza Automática** - Remove conexões inativas (5min timeout)

### Tipos de Dados Processados
1. **Dados da Bicicleta** - Status completo, posição, bateria
2. **Scans WiFi** - Redes detectadas com RSSI e coordenadas
3. **Alertas** - Notificações de bateria baixa e eventos críticos

## 🔧 Configuração

### Setup Inicial Automático (Primeira Vez)
1. **Flash firmware** → Central entra em modo AP automaticamente
2. **LED pisca alternado** → Indica modo de configuração
3. **Conectar no AP**: `BPR_Setup_XXXXXX` (senha: `bpr12345`)
4. **Acessar**: `http://192.168.4.1`
5. **Configurar via web**:
   - ID da Base (ex: ameciclo, cepas, ctresiste)
   - Nome da Base (ex: Ameciclo, CEPAS, CTResiste)
   - WiFi SSID e senha
   - Firebase Database URL
   - Firebase API Key
6. **Central reinicia** → Baixa configurações do Firebase ou cria nova base

### Configuração Dinâmica
- ✅ **Download automático** de configurações do Firebase
- ✅ **Criação automática** de nova base se não existir
- ✅ **Configuração remota** - mudanças via Firebase
- ✅ **Fallbacks seguros** - valores padrão se falhar
- ✅ **Cache local** - funciona offline

## 🔧 **Sistema de Configuração**

### **Configuração Inicial via AP**
```
Primeira vez → Modo AP → Interface Web → Configuração Completa
```

### **Download Automático do Firebase**
```
GET /central_configs/{base_id}.json  # Configuração completa da base
```

### **Estrutura de Configuração**
```json
{
  "base_id": "ameciclo",
  "wifi": {"ssid": "...", "password": "..."},
  "firebase": {"database_url": "...", "api_key": "..."},
  "central": {"name": "Ameciclo", "location": {...}},
  "sync_interval_sec": 300,
  "led_pin": 8,
  "led": {"boot_ms": 100, "ble_ready_ms": 2000, ...}
}
```

### **Heartbeat Automático**
```json
{
  "type": "central_heartbeat",
  "timestamp": 1764802387,
  "bikes_connected": 3,
  "heap": 144396,
  "uptime": 15
}
```

### **Envio para Bicicletas**
```cpp
struct BPRConfigPacket {
    uint8_t version;
    uint16_t deepSleepSec;
    uint16_t wifiScanInterval;
    uint16_t wifiScanLowBatt;
    float minBatteryVoltage;
    uint32_t timestamp;
};
```

## 📊 Estruturas de Dados

### Dados da Bicicleta (JSON)
```json
{
  "uid": "bike07",
  "base_id": "ameciclo",
  "battery_voltage": 3.82,
  "status": "active",
  "last_position": {
    "lat": -8.064,
    "lng": -34.882,
    "source": "wifi"
  },
  "last_ble_contact": 1733459190
}
```

### Scan WiFi (JSON)
```json
{
  "bike_id": "bike07",
  "timestamp": 1733459205,
  "networks": [
    {
      "ssid": "NET_5G",
      "bssid": "AA:BB:CC:11:22:33",
      "rssi": -70
    }
  ]
}
```

### Alerta de Bateria (JSON)
```json
{
  "type": "battery_alert",
  "bike_id": "bike07",
  "battery_voltage": 3.2,
  "critical": true
}
```

## 🚨 Processamento de Alertas

### Alertas Críticos (Sync Forçada)
- **Bateria Crítica** - Voltagem < 3.2V força sync imediata
- **Falha de Comunicação** - Timeout de conexão BLE

### Alertas Normais (Sync Agendada)
- **Bateria Baixa** - Voltagem < 3.45V
- **Dados Acumulados** - Buffer > limite ou timeout 5min
- **Conexão/Desconexão** - Bikes entrando/saindo da base

## 🔍 Monitoramento

O sistema monitora continuamente:
- **Heap Memory** - Uso de memória disponível
- **Modo Operacional** - BLE/WiFi/Shutdown
- **Bikes Conectadas** - Número e identificação individual
- **Status de Configuração** - Validade do cache de configs
- **Conexões BLE** - Handles ativos e inativos
- **Status WiFi** - Estado da conexão quando ativa
- **Buffer de Dados** - Volume de dados pendentes
- **Correção Temporal** - Timestamps corrigidos via NTP

### **Limpeza Automática**
- **Conexões Inativas** - Remove bikes sem atividade por 5min
- **Cache Expirado** - Revalida configurações a cada 1h
- **Memória** - Monitoramento contínuo de heap

## 🛠️ Build e Deploy

```bash
# Instalar dependências
pio lib install

# Compilar
pio run

# Upload firmware (primeira vez)
pio run --target upload

# Monitor serial
pio device monitor
```

### Primeira Configuração
1. **Flash firmware** → LED pisca alternado
2. **Conectar WiFi**: `BPR_Setup_XXXXXX` (senha: `bpr12345`)
3. **Acessar**: `http://192.168.4.1`
4. **Configurar** → Central reinicia automaticamente
5. **LED pisca lento** → Funcionamento normal

### Reconfiguração
- **Via Firebase** → Muda configurações remotamente
- **Via AP** → Delete `/config.json` e reinicie para voltar ao modo setup

## 📋 Logs

O sistema gera logs detalhados via Serial (115200 baud) incluindo:

### Eventos BLE
- 🔵 ✅ **BIKE CONECTADA** - Nova conexão estabelecida
- 🔴 ❌ **BIKE DESCONECTADA** - Conexão perdida
- 📝 ✅ **DADOS RECEBIDOS** - Processamento de dados JSON

### Dados Processados
- 😲 **DADOS DA BICICLETA** - Status completo da bike
- 📶 **SCAN WIFI** - Redes detectadas pela bike
- ⚠️ **ALERTA DE BATERIA** - Notificações críticas

### Sincronização
- 📶 **WiFi conectado** - Início da sincronização
- 📥 **Baixando configurações** - Download do Firebase
- 🔥 **Enviando dados** - Upload para Firebase
- 🔧 **Timestamp corrigido** - Correção temporal aplicada
- 📦 **Dados grandes** - Divisão em batches
- 📋 **Marcando bikes para reconfigurar** - Após update de configs

### Monitoramento
- 📊 **Heap/Modo/BLE/Bikes/Config** - Status completo a cada 15s
- 🔵 ✅ **Nova bike conectada** - Identificação e registro
- 🔴 ❌ **Bike desconectada** - Remoção do registro
- 📡 ✅ **Config enviada** - Confirmação de configuração
- 🧹 **Removendo bike inativa** - Limpeza automática
- ⚠️ **Timeouts e Erros** - Falhas de conexão
- 😨 **Alertas Críticos** - Situações que requerem ação