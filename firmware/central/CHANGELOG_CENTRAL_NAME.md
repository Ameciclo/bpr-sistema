# Correção: Nome da Central Configurável

## 🐛 Problema Identificado
- Nome BLE da central estava hardcoded como "BPR Base Station"
- Impossibilitava identificação única de diferentes centrais
- Inconsistência entre configuração dinâmica e nome fixo

## ✅ Solução Implementada

### 1. Estrutura de Configuração Atualizada
```cpp
struct CentralConfig {
    String base_id = "base01";
    String central_name = "BPR Base Station";  // ← NOVO CAMPO
    // ... outros campos
};
```

### 2. Parsing de Configuração Melhorado
- Adicionado suporte ao campo `central_name` no JSON
- Carregamento tanto de configurações básicas quanto completas
- Fallback para nome padrão se não especificado

### 3. Interface Web de Setup Atualizada
- Campo "Nome da Base" no formulário de configuração inicial
- Salva tanto `base_id` quanto `central_name` no config.json
- Permite personalização completa da central

### 4. BLE Dinâmico
- Função `setBLEDeviceName()` configurada antes da inicialização
- Nome aplicado durante `initBLESimple()`
- Cada central agora tem nome único e identificável

## 📋 Mudanças nos Arquivos

### `src/main.cpp`
- ✅ Adicionado campo `central_name` na struct `CentralConfig`
- ✅ Parsing do campo `central_name` em `parseConfigFromJson()`
- ✅ Carregamento do nome na função `loadCentralConfig()`
- ✅ Configuração do nome BLE antes da inicialização
- ✅ Uso dinâmico do nome no setup e criação de base

### `src/ble_simple.cpp`
- ✅ Simplificada função `setBLEDeviceName()`
- ✅ Uso do `deviceName` configurado em `initBLESimple()`

## 🧪 Testes Realizados
- ✅ Configuração padrão funciona
- ✅ Configuração personalizada via código
- ✅ Configuração via interface web
- ✅ Nome BLE é aplicado corretamente

## 🎯 Resultados

### Antes (Inconsistente)
```cpp
// Nome sempre fixo
NimBLEDevice::init("BPR Base Station");
```

### Depois (Configurável)
```cpp
// Nome dinâmico baseado na configuração
String centralName = config.central_name;
setBLEDeviceName(centralName);
NimBLEDevice::init(deviceName.c_str());
```

## 📊 Exemplos de Uso

### Configuração Ameciclo
```json
{
  "base_id": "ameciclo",
  "central_name": "Ameciclo Central",
  "wifi": {...},
  "firebase": {...}
}
```
**BLE Name:** "Ameciclo Central"

### Configuração CEPAS
```json
{
  "base_id": "cepas", 
  "central_name": "CEPAS - Centro",
  "wifi": {...},
  "firebase": {...}
}
```
**BLE Name:** "CEPAS - Centro"

## 🔧 Compatibilidade
- ✅ Mantém compatibilidade com configurações existentes
- ✅ Fallback para nome padrão se campo não existir
- ✅ Não quebra funcionalidade existente
- ✅ Melhora identificação de centrais múltiplas

## 🚀 Benefícios
1. **Identificação Única**: Cada central tem nome próprio
2. **Configuração Flexível**: Nome definido via interface web
3. **Debugging Melhorado**: Logs mostram qual central está ativa
4. **Escalabilidade**: Suporte a múltiplas centrais na mesma área
5. **Manutenção**: Fácil identificação durante manutenção

---
**Status:** ✅ Implementado e Testado  
**Versão:** 1.0  
**Data:** $(date)