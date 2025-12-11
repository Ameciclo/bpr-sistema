# BPR Hub - Central Redesenhada

Sistema central ESP32C3 redesenhado do zero com arquitetura modular baseada em estados.

## 🎯 Características Principais

- **Arquitetura por Estados**: Cada arquivo representa um estado específico
- **Zero Hardcoding**: Todas as constantes centralizadas em `constants.h`
- **LittleFS**: Gerenciamento de arquivos e configurações
- **Buffer Inteligente**: Armazena dados localmente antes da sincronização
- **WiFi/BLE Exclusivo**: Nunca funcionam simultaneamente
- **Configuração Dinâmica**: Todas as configs vêm do Firebase após setup inicial

## 📁 Estrutura de Arquivos

```
hub/
├── src/
│   ├── main.cpp              # 🚀 Ponto de entrada minimalista
│   ├── state_machine.cpp     # 🔄 Coordenador de estados
│   ├── config_manager.cpp    # ⚙️ Gerenciador de configurações
│   ├── config_ap.cpp         # 📱 Estado: Configuração via AP
│   ├── ble_only.cpp          # 🔵 Estado: Modo BLE puro
│   ├── wifi_sync.cpp         # 📡 Estado: Sincronização WiFi
│   ├── shutdown.cpp          # 💤 Estado: Economia de energia
│   ├── buffer_manager.cpp    # 📦 Buffer local de dados
│   ├── led_controller.cpp    # 💡 Controle de LED
│   └── self_check.cpp        # 🔧 Auto-diagnóstico
├── include/
│   ├── constants.h           # 🎯 Todas as constantes
│   ├── config_types.h        # 📋 Estruturas de dados
│   └── *.h                   # Headers dos módulos
└── data/                     # Arquivos do LittleFS
```

## 🔄 Fluxo de Estados

```
BOOT → CONFIG_AP (se config inválida)
     → BLE_ONLY (se config válida)

CONFIG_AP → BLE_ONLY (após configuração)

BLE_ONLY → WIFI_SYNC (timer ou buffer cheio)
         → SHUTDOWN (inatividade)

WIFI_SYNC → BLE_ONLY (após sync)

SHUTDOWN → BLE_ONLY (timer ou atividade)
```

## ⚙️ Configuração Inicial

1. **Primeira execução**: Entra em modo CONFIG_AP
2. **Conectar ao AP**: `BPR_HUB_CONFIG` / `bpr12345`
3. **Acessar interface**: `http://192.168.4.1`
4. **Configurar**: WiFi, Firebase, Base ID
5. **Reiniciar**: Entra em modo BLE_ONLY

## 🔵 Modo BLE_ONLY

- Servidor BLE ativo para comunicação com bikes
- Buffer local de dados recebidos
- LED indica status (bikes conectadas, heartbeat)
- Trigger automático para sincronização

## 📡 Modo WIFI_SYNC

- Conecta WiFi (BLE desabilitado)
- Sincroniza horário via NTP
- Download configurações do Firebase
- Upload dados do buffer
- Envia heartbeat
- Retorna para BLE_ONLY

## 💾 Gerenciamento de Dados

- **Buffer Local**: Armazena dados até sincronização
- **Persistência**: LittleFS para configs e buffer
- **Recuperação**: Carrega estado após reinicialização
- **Batching**: Upload em lotes para otimizar

## 💡 Sistema de LED

- **Boot**: Piscar rápido (100ms)
- **Config**: Piscar médio (200ms)
- **BLE Ready**: Piscar lento (2s)
- **Sync**: Piscar médio (500ms)
- **Error**: Piscar muito rápido (50ms)
- **Bike Arrived**: 3 piscadas rápidas
- **Bike Left**: 1 piscada longa
- **Count**: N piscadas = N bikes conectadas

## 🔧 Build e Deploy

```bash
cd firmware/hub
pio run --target upload
pio run --target uploadfs
```

## 🎯 Vantagens da Nova Arquitetura

- ✅ **Modular**: Cada estado é independente
- ✅ **Configurável**: Zero hardcoding
- ✅ **Robusto**: Auto-diagnóstico e recuperação
- ✅ **Eficiente**: Buffer local e sync inteligente
- ✅ **Simples**: Interface web para configuração
- ✅ **Escalável**: Fácil adicionar novos estados