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
│   ├── main.cpp           # Código principal
│   ├── ble_simple.cpp     # BLE management
│   ├── bike_manager.cpp   # Gerenciamento de bikes
│   └── config_manager.cpp # Configurações
├── data/
│   ├── config.json        # Config básica (criada pelo script)
│   └── firebase_config.json # Config Firebase
├── upload.sh              # Script de setup automático
└── platformio.ini         # Configuração PlatformIO
```

## 🚲 Funcionalidades

### Sistema de Descoberta de Bikes
- **Advertising BLE:** `BPR_BASE_{base_id}`
- **Detecção automática:** Bikes novas com prefixo `BPR_*`
- **Aprovação humana:** Via dashboard/bot
- **Configuração automática:** Após aprovação

### Modos de Operação
- **BLE Only:** Modo padrão (baixo consumo)
- **WiFi Sync:** Ativado quando necessário
- **Setup AP:** Primeira configuração

### Sistema de LED
- **Inicializando:** Piscar rápido (100ms)
- **BLE Ativo:** Piscar lento (2s)
- **Bike Chegou:** 3 piscadas rápidas
- **Bike Saiu:** 1 piscada longa
- **Contagem:** N piscadas = N bikes
- **Sincronizando:** Piscar médio (500ms)
- **Erro:** Piscar muito rápido (50ms)

## 🔗 Integrações

### Firebase
- **Configurações:** `/central_configs/{base_id}.json`
- **Bikes pendentes:** `/pending_bikes/{base_id}/`
- **Dados das bikes:** `/bikes/`, `/wifi_scans/`
- **Heartbeat:** `/bases/{base_id}/last_heartbeat`

### BLE
- **Service UUID:** `BAAD`
- **Características:** Bike ID (`F00D`), Battery (`BEEF`)
- **Advertising:** Nome da central + detecção de bikes

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

### Central não conecta WiFi
- Verificar SSID/senha em `data/config.json`
- Verificar sinal WiFi
- Logs: `pio device monitor`

### BLE não funciona
- Verificar se ESP32 suporta BLE
- Reiniciar ESP32
- Verificar logs de inicialização

### Bikes não detectadas
- Verificar se bike anuncia como `BPR_*`
- Verificar distância BLE (< 10m)
- Verificar logs de conexão BLE

### Firebase não sincroniza
- Verificar conexão WiFi
- Verificar URL/API key do Firebase
- Verificar logs de HTTPS

## 📊 Monitoramento

### Logs Importantes
```
✅ BLE OK                    # BLE inicializado
📡 Central anunciando como   # Nome BLE configurado
🆕 Nova bike detectada      # Bike nova encontrada
⏳ Bike registrada         # Aguardando aprovação
✅ WiFi conectado           # Sync ativa
💓 Heartbeat enviado        # Status da central
```

### Métricas
- Bikes conectadas
- Uso de memória (heap)
- Tempo de uptime
- Frequência de sync
- Bikes pendentes de aprovação