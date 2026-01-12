# 🔧 BPR Sistema - Firmware

Firmware completo para ESP32 do sistema BPR, incluindo bicicletas e centrais de coleta.

## 📁 Estrutura do Diretório

```
firmware/
├── bici/              # 🚲 Firmware das bicicletas (ESP32 genérico)
├── central/           # 🏢 Firmware da central (ESP32-WROOM-32D)
├── common/            # 🔗 Definições compartilhadas (BLE protocol)
└── README.md          # 📖 Este arquivo
```

## 🚲 Firmware Bicicleta (`bici/`)

**Plataforma**: ESP32 genérico  
**Função**: Scanner WiFi móvel com comunicação BLE

### Características Principais
- **Máquina de estados** bem definida (BOOT → CONFIG_REQUEST → SCANNING → AT_BASE → SLEEP)
- **Scanner WiFi automático** com intervalos configuráveis
- **Cliente BLE** para comunicação com central
- **Gerenciamento de energia** com deep sleep inteligente
- **Monitor de bateria** com alertas automáticos
- **Buffer local persistente** no LittleFS
- **ID único automático** baseado no chip ID (bpr-XXXXXX)

### Estrutura de Código
```
bici/src/
├── main.cpp              # 🚀 Máquina de estados principal
├── scanning.cpp          # 📡 Scanner WiFi com filtros RSSI
├── at_base.cpp           # 🔵 Cliente BLE para central
├── buffer_manager.cpp    # 📦 Buffer persistente LittleFS
├── config_manager.cpp    # ⚙️ Configurações dinâmicas
├── power_manager.cpp     # ⚡ Coordenação de rádios e sleep
├── lost.cpp              # 🔍 Estado de busca por central
└── utils.cpp             # 🛠️ Utilitários gerais
```

### Setup Rápido
```bash
cd firmware/bici
pio run --target uploadfs     # Upload config.json
pio run --target upload       # Upload código
pio device monitor            # Monitor serial
```

**📖 Documentação completa**: [bici/README.md](bici/README.md)

## 🏢 Firmware Central (`central/`)

**Plataforma**: ESP32-WROOM-32D  
**Função**: Base de coleta com validação e sincronização Firebase

### Características Principais
- **Arquitetura modular** baseada em estados bem definidos
- **Servidor BLE** para comunicação com bikes autorizadas
- **Sistema de validação** rigoroso (allowed/pending/blocked)
- **Configuração dinâmica** por base e por bike via Firebase
- **Push automático** de configurações via BLE
- **LED inteligente** com padrões de status (Pin 2 built-in)
- **Heartbeat automático** para monitoramento
- **Self-check** de hardware no boot
- **Timestamps precisos** adicionados pela central

### Estrutura de Código
```
central/src/
├── main.cpp                    # 🚀 Entry point + self-check
├── config_ap.cpp               # 📱 Estado: Configuração via AP
├── ble_server.cpp              # 🔵 Estado: Servidor BLE + filtros
├── cloud_sync.cpp              # 📡 Estado: Sincronização completa
├── buffer_manager.cpp          # 📦 Buffer local de dados
├── led_controller.cpp          # 💡 Padrões de LED inteligentes
├── bike_manager.cpp            # 🚲 Registro e validação de bikes
├── config_manager.cpp          # ⚙️ Configs dinâmicas por bike
└── self_check.cpp              # 🔧 Diagnóstico de hardware
```

### Setup Rápido
```bash
cd firmware/central
./setup.sh                    # Configurar WiFi, Firebase e Base ID
pio run --target uploadfs     # Upload configurações
pio run --target upload       # Upload código
pio device monitor            # Monitor serial
```

**📖 Documentação completa**: [central/README.md](central/README.md)

## 🔗 Definições Compartilhadas (`common/`)

**Função**: Protocolo BLE e estruturas de dados compartilhadas

### Arquivos
- **`bpr_protocol.h`**: UUIDs BLE, constantes de comunicação
- **`bpr_types.h`**: Estruturas de dados, utilitários de ID
- **`README.md`**: Documentação do protocolo

### Uso
```cpp
#include "../common/bpr_protocol.h"
#include "../common/bpr_types.h"

// Usar constantes BLE compartilhadas
NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

// Usar estruturas compartilhadas
WiFiRecord wifiBuffer[MAX_WIFI_NETWORKS_BLE];
String bikeId = generateBikeId();
```

**📖 Documentação completa**: [common/README.md](common/README.md)

## 🔄 Fluxo de Comunicação

```mermaid
flowchart TD
    subgraph "🚲 Bicicleta (ESP32)"
        A1[WiFi Scanner] --> A2[Buffer Local LittleFS]
        A2 --> A3[Detecta Central BLE]
        A3 --> A4[Envia via BLE]
    end
    
    subgraph "🏢 Central (ESP32-WROOM-32D)"
        A4 --> B1[Valida Bike]
        B1 --> B2{Status?}
        B2 -->|allowed| B3[Processa Dados]
        B2 -->|pending| B4[Registra Visita]
        B2 -->|blocked| B5[Rejeita]
        B3 --> B6[Adiciona Timestamp]
        B6 --> B7[Buffer Local]
        B7 --> B8[Sync Firebase]
    end
    
    subgraph "🔥 Firebase"
        B8 --> C1[Realtime Database]
        C1 --> C2[/bases/{id}/bikes]
        C1 --> C3[/bike_configs/{id}]
        C1 --> C4[/bikes/{id}/sessions]
    end
```

## 🔵 Protocolo BLE

### Service Principal
- **UUID**: `12345678-1234-1234-1234-123456789abc`
- **Nome**: `BPR Central` (anunciado pela central)

### Características
| Característica | UUID | Função |
|----------------|------|---------|
| **Data** | `12345678-1234-1234-1234-123456789abd` | Envio de dados bike → central |
| **Config** | `12345678-1234-1234-1234-123456789abe` | Configurações central → bike |

### Fluxo de Dados
1. **Bike detecta central** via scan BLE
2. **Conecta** e envia status via Data characteristic
3. **Envia dados WiFi** em lotes via Data characteristic
4. **Recebe configurações** via Config characteristic (notificações)
5. **Desconecta** quando central entra em modo WiFi sync

## ⚙️ Configuração do Sistema

### Configuração da Central
```json
{
  "base_id": "base01",
  "sync_interval_sec": 300,
  "wifi_timeout_sec": 30,
  "led_pin": 2,
  "firebase_batch_size": 8000
}
```

### Configuração por Bike
```json
{
  "version": 2,
  "bike_name": "Bike Centro 01",
  "wifi": {
    "scan_interval_sec": 300,
    "scan_timeout_ms": 5000,
    "max_networks": 20,
    "rssi_threshold": -90
  },
  "power": {
    "deep_sleep_duration_sec": 3600
  },
  "battery": {
    "critical_voltage": 3.2,
    "low_voltage": 3.45
  }
}
```

## 🛠️ Desenvolvimento

### Pré-requisitos
- **PlatformIO** (VS Code extension ou CLI)
- **ESP32-WROOM-32D** (para central)
- **ESP32** genérico (para bicicletas)
- **Conta Firebase** (para configurações)

### Workflow de Desenvolvimento
1. **Clone o repositório**
2. **Configure Firebase** (central/setup.sh)
3. **Upload filesystem** (configs locais)
4. **Upload firmware** (código principal)
5. **Monitor serial** (debug e logs)

### Debug e Logs
- **Serial 115200 baud** em ambos firmwares
- **Logs estruturados** com timestamps
- **LED de status** na central (Pin 2)
- **Modo dev_mode** para testes sem bateria

## 🔧 Troubleshooting

### Problemas Comuns

#### Bike não conecta na central
- ✅ Verificar nome BLE: `BPR Central`
- ✅ Confirmar UUIDs das características
- ✅ Central pode estar em modo WiFi sync
- ✅ Bike pode estar `blocked` no Firebase

#### Dados não chegam no Firebase
- ✅ Bike deve estar `allowed` (não `pending`)
- ✅ Verificar formato JSON dos dados
- ✅ Confirmar campo `bike_id` obrigatório
- ✅ Central deve ter conexão WiFi válida

#### Central não sincroniza
- ✅ Verificar credenciais WiFi e Firebase
- ✅ Confirmar `base_id` no Firebase
- ✅ Primeira sync é obrigatória após setup
- ✅ Verificar logs de erro no serial

### Comandos Úteis
```bash
# Monitor serial com filtro
pio device monitor --filter esp32_exception_decoder

# Upload apenas código (sem filesystem)
pio run --target upload

# Limpar build cache
pio run --target clean

# Verificar configuração
pio project config
```

## 📊 Métricas e Monitoramento

### Dados Coletados
- **WiFi scans**: Redes detectadas com RSSI, BSSID, canal
- **Bateria**: Voltagem e status de carregamento
- **Conexões**: Timestamps de conexão/desconexão
- **Heartbeats**: Status das bikes e central
- **Configurações**: Versões e mudanças aplicadas

### Estrutura Firebase
```
/bases/{base_id}/
├── configs/         # Configurações da central
├── bikes/          # Registro de bikes (allowed/pending/blocked)
└── last_heartbeat/ # Status da central

/bike_configs/{bike_id}/  # Configurações por bike

/bikes/{bike_id}/sessions/{session_id}/  # Dados coletados
```

## 🚀 Próximos Passos

### Melhorias Planejadas
- [ ] **OTA Updates**: Atualização remota de firmware
- [ ] **Mesh Network**: Comunicação entre centrais
- [ ] **GPS Integration**: Coordenadas precisas nas bikes
- [ ] **Advanced Filtering**: Filtros inteligentes de dados
- [ ] **Edge Analytics**: Processamento local na central

### Contribuição
1. **Fork** o projeto
2. **Crie branch** para sua feature
3. **Siga padrões** de código existentes
4. **Teste** em hardware real
5. **Documente** mudanças no README
6. **Abra Pull Request** com descrição detalhada

---

**📖 Para documentação específica de cada componente, consulte os READMEs individuais em cada diretório.**