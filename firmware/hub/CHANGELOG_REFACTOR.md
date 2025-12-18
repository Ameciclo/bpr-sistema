# 🔄 Refatoração de Nomenclatura - Hub → Central

## 📋 Resumo das Mudanças

### ✅ Estados da Máquina Renomeados
- `STATE_BLE_ONLY` → `STATE_BIKE_PAIRING`
- `STATE_WIFI_SYNC` → `STATE_CLOUD_SYNC`

### ✅ Arquivos Renomeados
- `ble_only.h` → `bike_pairing.h`
- `ble_only.cpp` → `bike_pairing.cpp`
- `wifi_sync.h` → `cloud_sync.h`
- `wifi_sync.cpp` → `cloud_sync.cpp`

### ✅ Classes Renomeadas
- `BLEOnly` → `BikePairing`
- `WiFiSync` → `CloudSync`
- `HubConfig` → `CentralConfig`

### ✅ Métodos Renomeados
- `getHubConfigUrl()` → `getCentralConfigUrl()`
- `downloadHubConfig()` → `downloadCentralConfig()`

### ✅ Strings e Mensagens Atualizadas
- "BPR Hub Station" → "BPR Central Station"
- "BPR_Hub_Config" → "BPR_Central_Config"
- "hub_default" → "central_default"
- "BLE_ONLY" → "BIKE_PAIRING" (logs)
- "WIFI_SYNC" → "CLOUD_SYNC" (logs)

### ✅ Arquivos Modificados
1. **constants.h** - Enum SystemState
2. **main.cpp** - Máquina de estados e getStateName()
3. **config_manager.h** - Struct e métodos
4. **config_manager.cpp** - Implementação
5. **config_ap.cpp** - Interface web e referências
6. **buffer_manager.cpp** - Correções de métodos

### ✅ Arquivos Removidos
- `include/ble_only.h`
- `include/wifi_sync.h`
- `src/ble_only.cpp`
- `src/wifi_sync.cpp`

## 🎯 Benefícios da Refatoração

### 🧠 Clareza Conceitual
- Estados representam **propósito** (pairing, sync) não **tecnologia** (BLE, WiFi)
- Nomenclatura consistente: "Central" em vez de "Hub"
- Código mais autodocumentado

### 🔧 Flexibilidade Técnica
- `BIKE_PAIRING` pode usar BLE hoje, LoRa amanhã
- `CLOUD_SYNC` pode usar WiFi hoje, 4G amanhã
- Facilita evolução sem quebrar conceitos

### 📚 Manutenibilidade
- Logs mais claros sobre o que está acontecendo
- Funções com nomes que explicam o propósito
- Estrutura mais lógica para novos desenvolvedores

## 🔄 Próximos Passos

### 🚧 Submáquina de Estados (Futuro)
O `BIKE_PAIRING` pode ser expandido com submáquina:
```cpp
enum PairingState {
    DISCOVERING_BIKES,
    VALIDATING_BIKES, 
    EXCHANGING_DATA,
    VERIFYING_INTEGRITY,
    READY_FOR_SYNC
};
```

### 📝 Documentação
- Atualizar README.md do firmware
- Atualizar diagramas de estados
- Documentar novos fluxos

## ✅ Status: CONCLUÍDO
Todas as mudanças foram aplicadas com sucesso. O sistema mantém a mesma funcionalidade com nomenclatura mais clara e conceitual.