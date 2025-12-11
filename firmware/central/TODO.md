# BPR Central System - TODO List

## 📊 Status Geral dos Arquivos

### ✅ **Arquivos Existentes (17/17)**
- [x] **main.cpp** - Ponto de entrada e loop principal
- [x] **state_machine.cpp/.h** - Máquina de estados do sistema  
- [x] **config_manager.cpp/.h** - Gerenciamento de configurações (unificar com central_config)
- [x] **ble_simple.cpp/.h** - Servidor BLE simplificado
- [x] **bike_manager.cpp/.h** - Gerenciamento de bikes conectadas
- [x] **bike_discovery.cpp/.h** - Descoberta automática de bikes (apenas prefixo)
- [x] **firebase_manager.cpp/.h** - Lógica de sincronização e batching
- [x] **firebase_client.h** - Cliente HTTP REST puro
- [x] **wifi_manager.cpp/.h** - Gerenciamento WiFi
- [x] **ntp_manager.cpp/.h** - Sincronização de horário (opcional)
- [x] **led_controller.cpp/.h** - Controle de LED com padrões
- [x] **buffer_manager.cpp/.h** - Gerenciamento de buffer local
- [x] **event_handler.cpp/.h** - Tratamento de eventos
- [x] **self_check.cpp/.h** - Auto-diagnóstico do sistema
- [x] **setup_server.cpp/.h** - Servidor AP para configuração inicial

### 🔥 **Arquivos Adicionais Recomendados**
- [ ] **errors.h** - Enum global de códigos de erro
- [ ] **watchdog.cpp/.h** - Watchdog timer para recovery

### ⚠️ **Arquivos para Refatorar/Unificar**
- 🔄 **config_loader.cpp/.h** → Unificar com config_manager (parsing apenas)
- 🔄 **central_config.cpp/.h** → Unificar com config_manager (schema)
- 🔄 **firebase_sync.h** → Mover lógica para firebase_manager

## 🔧 **Funcionalidades por Estado - Implementação Necessária**

### 1️⃣ **BOOT (main.cpp)**
- [ ] **Hardware Setup**: Inicialização LED, WiFi, BLE
- [ ] **Config Loading**: Integração com config_manager.loadConfig()
- [ ] **System Check**: Integração com self_check.systemCheck()
- [ ] **State Transition**: Lógica para CONFIG_AP vs BLE_ONLY
- [ ] **LED Boot Pattern**: Integração com led_controller.bootPattern()

### 2️⃣ **CONFIG_AP (setup_server.cpp)**
- [ ] **AP Creation**: Ponto de acesso para configuração inicial
- [ ] **Web Interface**: Formulário para WiFi + Firebase + base_id
- [ ] **Config Validation**: Validar dados recebidos
- [ ] **Config Save**: Integração com config_manager.saveConfig()
- [ ] **Restart Logic**: Reiniciar após configuração completa

### 3️⃣ **BLE_ONLY (Modo Principal)**
- [ ] **BLE Server Start**: ble_simple.startServer() com UUIDs corretos
- [ ] **Bike Discovery**: bike_discovery.scanForBikes() com prefixo "BPR_*"
- [ ] **Connection Handling**: bike_manager.handleConnections() 
- [ ] **Data Buffering**: buffer_manager.storeData() para cache local
- [ ] **LED Status**: led_controller.updateStatus() por número de bikes
- [ ] **Sync Triggers**: Condições para transição para WIFI_SYNC

### 4️⃣ **WIFI_SYNC (firebase_manager.cpp)**
- [ ] **WiFi Connection**: wifi_manager.connect() com timeout configurável
- [ ] **NTP Sync**: ntp_manager.syncTime() para timestamps corretos
- [ ] **Config Update**: config_loader.updateConfig() do Firebase
- [ ] **Data Upload**: firebase_manager.syncData() com batching
- [ ] **Buffer Clear**: buffer_manager.clearSent() após confirmação
- [ ] **Heartbeat**: Envio automático para /bases/{id}/last_heartbeat

### 5️⃣ **IDLE_MODE (state_machine.cpp)** ⚠️ *Renomeado de SHUTDOWN*
- [ ] **Inactivity Detection**: Timer para detectar falta de atividade
- [ ] **Light Sleep Only**: Manter WiFi+BLE ativos (central tem energia contínua)
- [ ] **Reduced Frequency**: Diminuir frequência de operações
- [ ] **Wake Conditions**: Atividade BLE ou timer periódico

## 🔵 **Sistema BLE - Implementações Necessárias**

### **ble_simple.cpp/.h**
- [ ] **Service Definition**: UUID do serviço BPR
- [ ] **Characteristics**: Status, Config, Data Transfer
- [ ] **Advertising**: Nome "BPR Base Station" + UUID
- [ ] **Connection Callbacks**: Integração com bike_manager
- [ ] **Data Reception**: Protocolo para receber dados das bikes
- [ ] **Config Sending**: Envio de configurações para bikes

### **bike_discovery.cpp/.h** 🎯 *Apenas descoberta*
- [ ] **Scan Logic**: Procurar devices com prefixo "BPR_*"
- [ ] **Detection Only**: NÃO armazenar estado das bikes
- [ ] **Event Trigger**: Notificar bike_manager sobre descobertas
- [ ] **Filter Logic**: Validar se é realmente uma bike BPR

### **bike_manager.cpp/.h**
- [ ] **Connection Pool**: Gerenciar múltiplas conexões simultâneas
- [ ] **Data Processing**: Processar dados recebidos das bikes
- [ ] **Status Tracking**: Estado de cada bike (conectada, última sync, etc.)
- [ ] **Event Generation**: Eventos para event_handler (chegada/saída)

## 🔥 **Sistema Firebase - Implementações Necessárias**

### **firebase_client.h** 🎯 *Camada de rede pura*
- [ ] **REST Functions**: GET, POST, PUT, PATCH puros
- [ ] **HTTP Client**: HTTPClient com SSL/TLS
- [ ] **JSON Parsing**: StaticJsonDocument (economia de RAM)
- [ ] **Connection Management**: Timeouts, retry básico

### **firebase_manager.cpp/.h** 🎯 *Lógica de sincronização*
- [ ] **Sync Strategy**: Batching, fila, rate limiting
- [ ] **Error Handling**: Retry inteligente, fallback para buffer
- [ ] **Data Upload**: Organizar dados em batches otimizados
- [ ] **Config Download**: Interface para config_manager
- [ ] **Heartbeat**: Envio automático de status
- [ ] **Offline Queue**: Armazenar quando sem internet

## 💡 **Sistema LED - Implementações Necessárias**

### **led_controller.cpp/.h**
- [ ] **Pattern Functions**: 
  - [ ] `bootPattern()` - 100ms rápido
  - [ ] `bleReadyPattern()` - 2s lento  
  - [ ] `bikeArrivedPattern()` - 3 piscadas rápidas
  - [ ] `bikeLeftPattern()` - 1 piscada longa
  - [ ] `countPattern(n)` - N piscadas = N bikes
  - [ ] `syncPattern()` - 500ms médio
  - [ ] `errorPattern()` - 50ms muito rápido
- [ ] **Non-blocking**: Usar timers, não delay()
- [ ] **State Management**: Controlar qual padrão está ativo
- [ ] **Priority System**: Padrões de erro têm prioridade

## ⚙️ **Sistema de Configuração - Implementações Necessárias**

### **config_manager.cpp/.h**
- [ ] **Config Structure**: Struct com todos os parâmetros
- [ ] **Default Values**: Valores padrão seguros
- [ ] **Validation**: Validar configurações recebidas
- [ ] **Persistence**: Salvar/carregar de SPIFFS/Preferences
- [ ] **Hot Reload**: Aplicar mudanças sem restart

### **config_manager.cpp/.h** 🔄 *Unificado*
- [ ] **Config Schema**: Struct completa com todos os parâmetros
- [ ] **Default Values**: Valores padrão seguros
- [ ] **Local Persistence**: Salvar/carregar de Preferences
- [ ] **Remote Download**: Baixar /central_configs/{base_id}.json via firebase_manager
- [ ] **Merge Logic**: Combinar local + remoto com fallback
- [ ] **Validation**: Ranges válidos, migration de schema
- [ ] **Hot Reload**: Aplicar mudanças sem restart

## 🔧 **Utilitários - Implementações Necessárias**

### **wifi_manager.cpp/.h**
- [ ] **Connection Logic**: Conectar com timeout configurável
- [ ] **Credential Management**: Salvar/carregar credenciais WiFi
- [ ] **Signal Monitoring**: Verificar qualidade do sinal
- [ ] **Reconnection**: Retry automático em caso de queda

### **ntp_manager.cpp/.h** ⚠️ *Opcional - não obrigatório*
- [ ] **Time Sync**: Sincronizar com servidor NTP configurável
- [ ] **Fallback**: Se falhar → usar millis() + offset salvo
- [ ] **Timezone**: Aplicar offset de fuso horário
- [ ] **Monotonic Time**: Horário relativo sempre funciona

### **buffer_manager.cpp/.h**
- [ ] **Circular Buffer**: Buffer otimizado para dados das bikes
- [ ] **Compression**: Compactar dados para economizar RAM
- [ ] **Persistence**: Salvar buffer crítico em flash
- [ ] **Batch Management**: Organizar dados em batches para upload

### **event_handler.cpp/.h**
- [ ] **Event Queue**: Fila de eventos assíncronos
- [ ] **Event Types**: Definir tipos (bike_arrived, bike_left, etc.)
- [ ] **Handlers**: Registrar handlers para cada tipo
- [ ] **Integration**: Integrar com LED, Firebase, etc.

### **self_check.cpp/.h**
- [ ] **Memory Check**: Verificar heap disponível
- [ ] **Hardware Check**: Testar LED, WiFi, BLE
- [ ] **Config Check**: Validar configuração carregada
- [ ] **Connectivity Check**: Testar conectividade básica
- [ ] **Error Reporting**: Reportar problemas via LED/Serial

## 📋 **Arquivos de Configuração Necessários**

### **platformio.ini** ⚠️ *Enxuto para ESP32-C3*
- [ ] ESP32-C3 SuperMini config
- [ ] Bibliotecas mínimas: WiFi, BLE, HTTPClient
- [ ] StaticJsonDocument (não ArduinoJson full)
- [ ] Flags de otimização de RAM
- [ ] Stack size reduzido

### **include/errors.h** 🆕 *Novo*
- [ ] Enum global de códigos de erro
- [ ] ERR_WIFI_CONNECT, ERR_FIREBASE_TIMEOUT, etc.
- [ ] Facilita LED controller e logs

### **include/config.h**
- [ ] Constantes globais (timeouts, buffers, etc.)
- [ ] LED_PIN = 8 (ESP32-C3 SuperMini)
- [ ] BLE UUIDs, limites de memória
- [ ] Evitar dependências circulares

### **data/config.json** (Preferences)
- [ ] Configuração inicial básica
- [ ] WiFi credentials, Firebase config, base_id

## 🧪 **Testes e Validação Necessários**

### **Testes Unitários**
- [ ] Cada módulo isoladamente
- [ ] Mock de dependências externas
- [ ] Validação de edge cases
- [ ] Testes de performance

### **Testes de Integração**
- [ ] Fluxo completo BLE_ONLY → WIFI_SYNC → BLE_ONLY
- [ ] Comunicação com bike simulada
- [ ] Upload para Firebase real
- [ ] Recovery após falhas

### **Testes de Sistema**
- [ ] Múltiplas bikes simultâneas
- [ ] Stress test de conectividade
- [ ] Teste de autonomia energética
- [ ] Validação de heartbeat
- [ ] Recovery após falhas de rede
- [ ] Teste de memória (leak detection) bikes simultâneas
- [ ] Stress test de conectividade
- [ ] Teste de autonomia energética
- [ ] Validação de heartbeat

## 🎯 **Prioridades Otimizadas - 100% Eficientes**

### **Fase 1 - Infra Básica ESP32-C3 (3-4 dias)**
1. **led_controller.cpp**: Padrões básicos (teste no 1º dia)
2. **config_manager.cpp**: Config local apenas
3. **wifi_manager.cpp**: Conexão básica
4. **ble_simple.cpp**: Advertising básico
🔎 *Resultado: Central "viva" e testável*

### **Fase 2 - State Machine (3-4 dias)**
1. **main.cpp**: Loop com estados BOOT/CONFIG_AP/BLE_ONLY
2. **state_machine.cpp**: Transições básicas
3. **setup_server.cpp**: AP para configuração
🔎 *Resultado: Central operante, descobrindo bikes*

### **Fase 3 - BLE Completo (5-6 dias)**
1. **bike_discovery.cpp**: Descoberta "BPR_*"
2. **bike_manager.cpp**: Gerenciamento de conexões
3. **buffer_manager.cpp**: Cache local
4. **event_handler.cpp**: Eventos bike arrived/left
5. LED integrando com eventos
🔎 *Resultado: Base interagindo com bikes reais*

### **Fase 4 - Firebase (5-6 dias)**
1. **firebase_client.h**: Cliente HTTP REST
2. **firebase_manager.cpp**: Sync e batching
3. **config_manager.cpp**: Download de configs remotas
4. **ntp_manager.cpp**: Sync opcional de horário
🔎 *Resultado: Base online e útil*

### **Fase 5 - Otimizações Finais (3-4 dias)**
1. **watchdog.cpp**: Watchdog timer
2. **self_check.cpp**: Auto-diagnóstico
3. **errors.h**: Códigos de erro globais
4. Testes e validação
🔎 *Resultado: Firmware profissional*

## ⚠️ **Considerações Críticas para ESP32-C3**

### 💾 **Limitações de Memória**
- **RAM**: ~400KB disponível, usar StaticJsonDocument
- **Flash**: Otimizar bibliotecas, evitar bloat
- **Stack**: Reduzir stack size, evitar recursão profunda

### 🔄 **Evitar Dependências Circulares**
```
main → managers
managers → utils  
utils → nada

❌ bike_manager NÃO deve chamar firebase_manager
❌ config_manager NÃO deve chamar firebase_manager diretamente
❌ ble_simple NUNCA deve conhecer firebase_manager
```

### 📡 **Coordenação BLE + WiFi**
- Evitar uso simultâneo intenso
- Firebase sync em janelas dedicadas
- BLE sempre prioritário (bikes conectadas)

### 🔧 **Dependências Mínimas**
- WiFi, BLE (built-in)
- HTTPClient (built-in)
- Preferences (built-in)
- StaticJsonDocument (leve)
- **Evitar**: ArduinoJson full, bibliotecas pesadas

### 🚪 **Recovery Robusto**
- Watchdog timer obrigatório
- Estados sempre recuperáveis
- Fallbacks para todas as operações críticas
- LED como único feedback confiável

---

**Status**: 📁 Arquivos criados (17/17) - 🔧 Implementação refinada necessária
**Estimativa**: 3-4 semanas com arquitetura otimizada