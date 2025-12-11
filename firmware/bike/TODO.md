# BPR Bike System - TODO List

## 🚀 Implementação Prioritária

### 📁 **Arquivos Core (6/6 definidos)**
- [ ] **main.cpp** - Loop principal e máquina de estados
- [ ] **wifi_scanner.cpp/.h** - Scanner WiFi com cache local
- [ ] **ble_client.cpp/.h** - Cliente BLE para comunicação
- [ ] **battery_monitor.cpp/.h** - Monitor de bateria e alertas
- [ ] **power_manager.cpp/.h** - Gerenciamento de energia/sleep
- [ ] **config_manager.cpp/.h** - Configurações dinâmicas

## 🔧 **Funcionalidades por Módulo**

### 1️⃣ **main.cpp**
- [ ] Setup inicial do hardware (LED, botão, ADC)
- [ ] Máquina de estados (BOOT → AT_BASE → SCANNING → LOW_POWER → DEEP_SLEEP)
- [ ] Loop principal com transições de estado
- [ ] Modo emergência (botão BOOT)
- [ ] Status periódico (30s) via Serial
- [ ] Indicadores LED por estado
- [ ] Tratamento de wake-up após deep sleep

### 2️⃣ **config_manager.cpp/.h**
- [ ] Estrutura `BikeConfig` (scan intervals, battery thresholds, etc.)
- [ ] Carregamento de configuração padrão
- [ ] Recebimento de config via BLE da base
- [ ] Aplicação dinâmica de configurações
- [ ] Salvamento de estado antes de deep sleep
- [ ] Validação de configurações recebidas

### 3️⃣ **battery_monitor.cpp/.h**
- [ ] Estrutura `BatteryData` (voltage, percentage, charging)
- [ ] Leitura ADC da tensão da bateria
- [ ] Cálculo de percentual baseado em curva LiPo
- [ ] Detecção de carregamento (via pino ou tensão)
- [ ] Thresholds para LOW_POWER e DEEP_SLEEP
- [ ] Histórico de leituras para média
- [ ] Alertas de bateria crítica

### 4️⃣ **wifi_scanner.cpp/.h**
- [ ] Estrutura `WifiRecord` (timestamp, bssid, rssi, channel)
- [ ] Buffer circular para armazenamento local
- [ ] Scan WiFi periódico com configuração dinâmica
- [ ] Filtros de qualidade (RSSI mínimo)
- [ ] Compressão/otimização de dados
- [ ] Envio de dados via BLE (batches)
- [ ] Limpeza de buffer após sincronização
- [ ] Tratamento de buffer cheio (sobrescrever antigos)

### 5️⃣ **ble_client.cpp/.h**
- [ ] Estrutura `BikeStatus` (bike_id, battery, records_count, etc.)
- [ ] Scan BLE por "BPR Base Station"
- [ ] Conexão automática quando base detectada
- [ ] Envio de status da bike
- [ ] Recebimento de configurações
- [ ] Transferência de dados WiFi (protocolo de batches)
- [ ] Tratamento de desconexões
- [ ] Retry automático em caso de falha

### 6️⃣ **power_manager.cpp/.h**
- [ ] Estrutura `PowerState` (current_state, timing, consumo)
- [ ] Coordenação de rádio WiFi/BLE (delay 200-300ms)
- [ ] Light sleep entre operações
- [ ] Deep sleep com wake-up timer
- [ ] Scaling dinâmico de CPU (80MHz/160MHz)
- [ ] Controle de TX power WiFi (-1dBm)
- [ ] Monitoramento de consumo por estado
- [ ] Otimizações baseadas em bateria

## ⚡ **Funcionalidades Críticas Faltando**

### 🔄 **Máquina de Estados**
- [ ] Enum para estados (BOOT, AT_BASE, SCANNING, LOW_POWER, DEEP_SLEEP)
- [ ] Função de transição entre estados
- [ ] Timeouts para cada estado
- [ ] Condições de transição baseadas em bateria/tempo
- [ ] Estado de erro/recovery

### 📡 **Protocolo BLE**
- [ ] Definição de UUIDs de serviço e características
- [ ] Protocolo de handshake com a base
- [ ] Formato de mensagens (status, config, data)
- [ ] Controle de fluxo para grandes transferências
- [ ] Checksums/validação de dados
- [ ] Versionamento de protocolo

### 🔋 **Gestão de Energia Avançada**
- [ ] Curva de descarga LiPo específica
- [ ] Predição de autonomia restante
- [ ] Ajuste automático de intervalos baseado em bateria
- [ ] Hibernação inteligente (condições múltiplas)
- [ ] Wake-up por múltiplas fontes (timer, botão, movimento?)

### 📊 **Monitoramento e Debug**
- [ ] Logs estruturados com níveis (DEBUG, INFO, WARN, ERROR)
- [ ] Métricas de performance (scans/min, conexões BLE, etc.)
- [ ] Estatísticas de uso de memória
- [ ] Contadores de erro por tipo
- [ ] Watchdog timer para recovery automático

## 🛠️ **Implementação Técnica**

### 📦 **Estruturas de Dados**
- [ ] Definir tamanhos máximos de buffers
- [ ] Otimizar structs para alinhamento de memória
- [ ] Implementar serialização para BLE
- [ ] Versionamento de estruturas de dados

### 🔧 **Configuração de Hardware**
- [ ] Definir pinos (LED, botão, ADC bateria)
- [ ] Configurar parâmetros BLE (advertising, connection)
- [ ] Otimizar configurações WiFi (canais, power)
- [ ] Calibração ADC para leitura precisa de bateria

### 🚨 **Tratamento de Erros**
- [ ] Códigos de erro padronizados
- [ ] Recovery automático por tipo de erro
- [ ] Fallbacks seguros (configurações padrão)
- [ ] Logs de erro para debug remoto

## 📋 **Arquivos de Configuração**

### 🔧 **platformio.ini**
- [ ] Configurações específicas ESP32-C3
- [ ] Bibliotecas necessárias (BLE, WiFi, ArduinoJson?)
- [ ] Flags de compilação para otimização
- [ ] Configurações de upload e monitor

### 📄 **Arquivos de Header**
- [ ] **config.h** - Constantes globais e configurações
- [ ] **types.h** - Definições de estruturas de dados
- [ ] **pins.h** - Mapeamento de pinos
- [ ] **constants.h** - Valores padrão e limites

## 🧪 **Testes e Validação**

### ✅ **Testes Unitários**
- [ ] Teste de cada módulo isoladamente
- [ ] Simulação de condições de erro
- [ ] Validação de transições de estado
- [ ] Teste de limites de buffer

### 🔍 **Testes de Integração**
- [ ] Comunicação BLE com base simulada
- [ ] Ciclo completo de coleta e sincronização
- [ ] Teste de autonomia e consumo
- [ ] Validação de recovery após deep sleep

## 📈 **Otimizações Futuras**

### 🚀 **Performance**
- [ ] Compressão de dados WiFi
- [ ] Cache inteligente de redes conhecidas
- [ ] Predição de localização de bases
- [ ] Otimização de algoritmos de scan

### 🔋 **Energia**
- [ ] Análise de consumo real vs teórico
- [ ] Ajuste fino de timings de sleep
- [ ] Implementação de modos de energia customizados
- [ ] Monitoramento de degradação da bateria

### 📡 **Conectividade**
- [ ] Suporte a múltiplas bases
- [ ] Roaming inteligente entre bases
- [ ] Backup de dados em caso de falha de sync
- [ ] Compressão de protocolo BLE

## 🎯 **Prioridades de Implementação**

### **Fase 1 - MVP (Minimum Viable Product)**
1. main.cpp com estados básicos
2. config_manager.cpp com configurações padrão
3. battery_monitor.cpp com leitura básica
4. wifi_scanner.cpp com scan simples
5. ble_client.cpp com comunicação básica
6. power_manager.cpp com sleep básico

### **Fase 2 - Funcionalidades Core**
1. Máquina de estados completa
2. Protocolo BLE robusto
3. Gestão de energia otimizada
4. Buffer e sincronização confiáveis

### **Fase 3 - Otimizações**
1. Coordenação de rádio WiFi/BLE
2. Monitoramento avançado
3. Recovery automático
4. Testes e validação completos

## 📝 **Notas de Implementação**

### ⚠️ **Considerações Técnicas**
- **ESP32-C3**: Delay obrigatório entre WiFi e BLE (200-300ms)
- **Memória**: Buffer circular para otimizar uso de RAM
- **Bateria**: Curva LiPo específica para cálculo de percentual
- **BLE**: Chunking para transferências grandes de dados
- **Watchdog**: Essencial para recovery em produção

### 🔧 **Dependências Externas**
- Biblioteca BLE ESP32
- WiFi ESP32 (built-in)
- ArduinoJson (para serialização?)
- Preferences (para persistência)
- Possível biblioteca de compressão

---

**Status**: 📋 Planejamento completo - Pronto para implementação
**Estimativa**: ~2-3 semanas para MVP, +2 semanas para versão completa