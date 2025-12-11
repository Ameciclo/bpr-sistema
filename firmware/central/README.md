# 🚲 BPR Central - Firmware

Central inteligente para monitoramento de bicicletas via BLE e WiFi.

## 🚀 Setup Rápido

### 1. Upload Automático
```bash
./upload.sh
```

O script fará:
- ✅ Solicitar configurações (Base ID, WiFi, etc)
- ✅ Criar arquivos de configuração
- ✅ Compilar firmware
- ✅ Upload filesystem + firmware
- ✅ Central pronta para uso!

### 2. Monitorar Logs
```bash
pio device monitor
```

## 📋 Configuração Manual

### Pré-requisitos
```bash
pip install platformio
```

### Passos
1. **Criar configuração:**
```bash
mkdir -p data
# Editar data/config.json com suas configurações
```

2. **Upload:**
```bash
pio run --target uploadfs  # Configurações
pio run --target upload     # Firmware
```

## 🔧 Estrutura de Arquivos

```
firmware/central/
├── src/
│   ├── main.cpp              # 🚀 Ponto de entrada e máquina de estados
│   ├── ble_simple.cpp        # 🔵 Servidor BLE simplificado
│   ├── bike_manager.cpp      # 🚲 Gerenciamento de bikes conectadas
│   ├── bike_discovery.cpp    # 🔍 Descoberta de bikes novas
│   ├── firebase_manager.cpp  # 🔥 Sync com Firebase
│   ├── led_controller.cpp    # 💡 Controle de LED com padrões
│   ├── state_machine.cpp     # 🔄 Máquina de estados do sistema
│   ├── config_manager.cpp    # ⚙️ Configurações dinâmicas
│   ├── ntp_manager.cpp       # ⏰ Sincronização de horário
│   ├── setup_server.cpp      # 🌐 AP para configuração inicial
│   ├── central_config.cpp    # 📋 Configuração da central
│   ├── buffer_manager.cpp    # 📦 Gerenciamento de buffers
│   ├── event_handler.cpp     # 🎯 Manipulação de eventos
│   ├── self_check.cpp        # 🔍 Auto-diagnóstico
│   └── wifi_manager.cpp      # 📶 Gerenciamento WiFi
├── include/
│   ├── config.h              # Definições de configuração
│   └── structs.h             # Estruturas de dados
├── data/
│   └── config.json           # Config básica (criada pelo script)
├── upload.sh                 # Script de setup automático
├── setup.sh                  # Script de configuração inicial
├── erase.sh                  # Script para apagar flash
└── platformio.ini            # Configuração PlatformIO
```

## 🚲 Funcionalidades

### 🔍 Sistema de Descoberta de Bikes (`bike_discovery.cpp`)
- **Advertising BLE:** `BPR_BASE_{base_id}`
- **Detecção automática:** Bikes novas com prefixo `BPR_*`
- **Aprovação humana:** Via dashboard/bot
- **Configuração automática:** Após aprovação
- **Registro no Firebase:** Bikes pendentes de aprovação

### 🔄 Máquina de Estados (`state_machine.cpp`)
- **MODE_SETUP_AP:** Configuração inicial via AP
- **MODE_BLE_ONLY:** Modo padrão (baixo consumo)
- **MODE_WIFI_SYNC:** Sincronização com Firebase
- **MODE_SHUTDOWN:** Desligamento controlado

### 🚲 Gerenciamento de Bikes (`bike_manager.cpp`)
- **Controle de conexões:** Até 10 bikes simultâneas
- **Envio de configurações:** Parâmetros dinâmicos
- **Monitoramento de bateria:** Alertas automáticos
- **Cleanup automático:** Remove conexões inativas

### 💡 Sistema de LED (`led_controller.cpp`)
- **Inicializando:** Piscar rápido (100ms)
- **BLE Ativo:** Piscar lento (2s)
- **Bike Chegou:** 3 piscadas rápidas
- **Bike Saiu:** 1 piscada longa
- **Contagem:** N piscadas = N bikes (a cada 30s)
- **Sincronizando:** Piscar médio (500ms)
- **Erro:** Piscar muito rápido (50ms)

### 🔥 Sincronização Firebase (`firebase_manager.cpp`)
- **Upload de dados:** Batch otimizado
- **Download de configs:** Configurações dinâmicas
- **Heartbeat:** Status da central
- **Correção de timestamp:** Sincronização NTP

### ⏰ Sincronização de Horário (`ntp_manager.cpp`)
- **Servidor NTP:** Configurável por base
- **Fuso horário:** GMT-3 (configurável)
- **Correção automática:** Timestamps precisos

### 🌐 Servidor de Configuração (`setup_server.cpp`)
- **Access Point:** Para primeira configuração
- **Interface web:** Configuração WiFi e Firebase
- **Validação:** Testa conectividade antes de salvar

## 🔗 Integrações

### 🔥 Firebase Realtime Database
- **Configurações:** `/central_configs/{base_id}.json`
- **Bikes pendentes:** `/pending_bikes/{base_id}/`
- **Dados das bikes:** `/bikes/{bike_id}/sessions/`
- **Scans WiFi:** `/bikes/{bike_id}/sessions/{session}/scans/`
- **Heartbeat:** `/bases/{base_id}/last_heartbeat`
- **Configuração global:** `/config`

### 🔵 Protocolo BLE
- **Service UUID:** `BAAD`
- **Características:**
  - Bike ID: `F00D` (identificação)
  - Battery: `BEEF` (dados de bateria)
- **Advertising:** Nome configurável da central
- **Descoberta:** Detecção automática de bikes `BPR_*`

### 📶 Conectividade WiFi
- **Modo Station:** Conexão com rede configurada
- **Modo AP:** Para configuração inicial
- **Timeout configurável:** 30s padrão
- **Reconexão automática:** Em caso de falha

## 🛠️ Desenvolvimento

### Build
```bash
pio run
```

### Upload
```bash
pio run --target upload
```

### Monitor
```bash
pio device monitor --baud 115200
```

### Clean
```bash
pio run --target clean
```

## 🐛 Troubleshooting

### ❌ Problemas Comuns

#### Central não conecta WiFi
- Verificar SSID/senha em `data/config.json`
- Verificar sinal WiFi (RSSI > -70dBm)
- Testar credenciais manualmente
- Logs: `pio device monitor`

#### BLE não funciona
- Verificar se ESP32 suporta BLE (ESP32C3 ✅)
- Reiniciar ESP32 (botão RST)
- Verificar logs de inicialização BLE
- Distância máxima: 10 metros

#### Bikes não detectadas
- Verificar se bike anuncia como `BPR_*`
- Verificar distância BLE (< 10m)
- Verificar se bike está em modo descoberta
- Logs de conexão BLE no monitor

#### Firebase não sincroniza
- Verificar conexão WiFi ativa
- Verificar URL/credenciais do Firebase
- Verificar certificados SSL
- Logs de HTTPS no monitor

#### LED não funciona
- Verificar pino configurado (padrão: 8)
- Verificar se LED está conectado corretamente
- Testar com LED interno (pino 2)

#### Configuração perdida
- Executar `./erase.sh` para limpar flash
- Executar `./setup.sh` para reconfigurar
- Verificar se `data/config.json` existe

#### Memória insuficiente
- Monitorar heap livre nos logs
- Reduzir `firebase_batch_size` na config
- Reiniciar central periodicamente

## 📊 Monitoramento e Debugging

### 📊 Logs e Monitoramento

#### Logs Importantes
```
🚀 BPR Central - HUB INTELIGENTE  # Inicialização
✅ LittleFS OK                    # Sistema de arquivos
✅ BLE OK                         # BLE inicializado
📡 Central anunciando como        # Nome BLE configurado
🆕 Nova bike detectada           # Bike nova encontrada
⏳ Bike registrada              # Aguardando aprovação
🔵 ✅ Nova bike conectada        # Bike aprovada conectou
🔴 ❌ Bike desconectada          # Bike desconectou
✅ WiFi conectado                # Sync ativa
💓 Heartbeat enviado             # Status da central
📊 Heap: XXXX | Modo: BLE        # Status periódico
```

#### Métricas Monitoradas
- **Bikes conectadas:** Número atual de bikes ativas
- **Uso de memória:** Heap livre em bytes
- **Tempo de uptime:** Segundos desde inicialização
- **Modo atual:** BLE_ONLY, WIFI_SYNC, SETUP_AP
- **Frequência de sync:** Intervalo configurável
- **Bikes pendentes:** Aguardando aprovação
- **Status de configuração:** Válida/inválida
- **Conectividade WiFi:** Status da conexão