# 🔍 Análise Sistemática de Protocolos BLE

## 📋 Status Atual da Análise

### ✅ Arquivos Mapeados
- [x] `/firmware/hub/src/ble_only.cpp` - Servidor BLE do Hub
- [x] `/firmware/bike/src/ble_client.cpp` - Cliente BLE da Bici
- [ ] Headers e constantes
- [ ] Configurações e UUIDs
- [ ] Fluxos de estado

## 🔄 Protocolo Esperado vs Implementado

### **1. Descoberta e Conexão**

#### 🎯 **Fluxo Esperado:**
```
1. Hub: Inicia advertising com nome "BPR_Hub_[ID]"
2. Bici: Scan por dispositivos "BPR_Hub_*"
3. Bici: Conecta no hub encontrado
4. Hub: Aceita conexão e incrementa contador
5. Ambos: Estabelecem características BLE
```

#### ⚠️ **Problemas Identificados:**
- **Hub**: Usa `BLE_DEVICE_NAME` (não sabemos o valor)
- **Bici**: Procura por `baseName` (pode não coincidir)
- **Inconsistência**: Nomes podem não bater

### **2. Registro da Bicicleta**

#### 🎯 **Fluxo Esperado:**
```json
Bici → Hub: {
  "type": "bike_registration",
  "bike_id": "intenso",
  "timestamp": 1733459200,
  "version": "2.0"
}
```

#### ⚠️ **Problemas Identificados:**
- **Hub**: Não processa `bike_registration` explicitamente
- **Bici**: Envia registro mas não aguarda confirmação
- **Falta**: Sistema de aprovação/whitelist

### **3. Troca de Configurações**

#### 🎯 **Fluxo Esperado:**
```json
Bici → Hub: {
  "type": "config_request",
  "bike_id": "intenso"
}

Hub → Bici: {
  "type": "config_response",
  "bike_id": "intenso",
  "config": { ... }
}
```

#### ⚠️ **Problemas Identificados:**
- **Hub**: Usa característica separada para config
- **Bici**: Não implementa solicitação de config
- **Inconsistência**: Protocolos diferentes

### **4. Envio de Dados WiFi**

#### 🎯 **Fluxo Esperado:**
```json
Bici → Hub: {
  "bike_id": "intenso",
  "networks": [
    {
      "bssid": "AA:BB:CC:DD:EE:FF",
      "rssi": -70,
      "channel": 6,
      "timestamp": 1733459205
    }
  ],
  "total_records": 1,
  "timestamp": 1733459200
}
```

#### ✅ **Status**: Implementado corretamente

## 🚨 **Problemas Críticos Identificados**

### **P1: Inconsistência de Nomes BLE**
- Hub usa constante não definida
- Bici procura por nome configurável
- **Solução**: Padronizar nomenclatura

### **P2: Protocolos de Config Diferentes**
- Hub usa característica `BLE_CHAR_CONFIG_UUID`
- Bici usa característica `BLE_DATA_CHAR_UUID`
- **Solução**: Unificar protocolo

### **P3: Falta Sistema de Aprovação**
- Hub não valida bikes conectadas
- Não há whitelist funcional
- **Solução**: Implementar validação

### **P4: UUIDs Não Verificados**
- Constantes podem estar diferentes
- **Próximo**: Verificar definições

## 📝 **Próximos Passos**

### **Etapa 1**: Verificar Constantes
- [ ] Mapear todos os UUIDs
- [ ] Verificar nomes de dispositivos
- [ ] Comparar definições

### **Etapa 2**: Testar Comunicação Básica
- [ ] Criar teste de descoberta
- [ ] Validar conexão simples
- [ ] Verificar troca de mensagens

### **Etapa 3**: Corrigir Protocolos
- [ ] Padronizar nomenclatura
- [ ] Unificar características
- [ ] Implementar validação

### **Etapa 4**: Testes Integrados
- [ ] Teste completo hub ↔ bici
- [ ] Validar todos os fluxos
- [ ] Documentar funcionamento

## 🔧 **Ferramentas de Debug**

### **Logs Estruturados**
```cpp
// Adicionar em ambos os firmwares
#define DEBUG_BLE 1
#if DEBUG_BLE
  #define BLE_LOG(fmt, ...) Serial.printf("[BLE] " fmt "\n", ##__VA_ARGS__)
#else
  #define BLE_LOG(fmt, ...)
#endif
```

### **Monitor Serial Duplo**
- Terminal 1: Hub
- Terminal 2: Bici
- Comparar logs em tempo real

### **Emulador para Testes**
- Usar emulador existente
- Simular cenários específicos
- Validar correções