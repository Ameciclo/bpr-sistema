# 🧪 BPR Sistema Emulador v2.0

Emulador completo do sistema BPR que simula o comportamento dos hubs, bicis e Firebase de forma offline, atualizado para as novas arquiteturas.

## 🚀 Como usar

```bash
cd emulator
npm install
npm start
```

## 🎯 Cenários Disponíveis

### 🏢 Hub inicializando e configurando
- Simula boot do hub ESP32C3
- Máquina de estados (CONFIG_AP → BLE_ONLY → WIFI_SYNC)
- Carregamento de configurações do Firebase
- Sistema de LED inteligente
- Buffer local e sincronização

### 🚲 Bici conectando no hub
- Boot da bicicleta ESP32/ESP8266
- Estados: BOOT → CONFIG_REQUEST → AT_BASE → SCANNING
- Descoberta e conexão BLE com hub
- Solicitação de configuração dinâmica
- Coordenação de rádio WiFi/BLE

### 🔄 Fluxo completo: Hub + Bici + Viagem
- Hub inicializado em modo BLE_ONLY
- Bici conecta e recebe configuração
- Bici entra em modo SCANNING
- Scans WiFi com buffer local
- Retorno à base e sincronização
- Hub faz WIFI_SYNC para upload

### 🔋 Teste de bateria baixa
- Simula bici com bateria baixa
- Transição para modo LOW_POWER
- Envio de alerta via BLE
- Processamento pelo hub
- Eventual DEEP_SLEEP

### 📡 Múltiplas bicis simultâneas
- 3 bicis conectando simultaneamente
- Atividade paralela com estados independentes
- Gerenciamento de múltiplas conexões BLE
- LED de contagem no hub

### ⚙️ Solicitação de configuração
- Bici nova sem configuração
- Estado CONFIG_REQUEST
- Comunicação BLE para receber config
- Aplicação e salvamento da configuração

## 🔧 Arquitetura

### Classes Principais

#### `BPREmulator`
- Orquestra os cenários
- Gerencia hub e bicis
- Interface com usuário

#### `Hub`
- Simula firmware do hub ESP32C3
- Máquina de estados modular
- Sistema de LED inteligente
- Servidor BLE para bicis
- Buffer local e sincronização WiFi
- Heartbeat automático

#### `Bici`
- Simula firmware da bici ESP32/ESP8266
- Máquina de estados otimizada
- Scans WiFi com buffer local
- Cliente BLE para comunicação
- Gerenciamento de energia
- Configuração dinâmica via BLE

#### `MockFirebase`
- Simula Firebase Realtime Database
- Estrutura de dados completa
- Operações CRUD
- Logs detalhados

## 📊 Dados Simulados

### Configurações
- Configurações globais do sistema
- Configurações específicas por hub
- Parâmetros de LED, WiFi, BLE
- Configuração dinâmica de bicis

### Dados Operacionais
- Status dos hubs e bicis
- Scans WiFi com redes fictícias
- Buffer local e sincronização
- Viagens com rotas e métricas
- Alertas de sistema
- Heartbeats automáticos

### Métricas
- Voltagem de bateria realística
- Estados de máquina detalhados
- Coordenação de rádio WiFi/BLE
- Consumo de energia simulado
- Posições GPS simuladas

## 🎮 Interação

O emulador mostra em tempo real:
- 🔵 Logs do hub (azul)
- 🔵 Logs das bicis (ciano)  
- 🔵 Logs do Firebase (cinza)
- 🔵 Estados da máquina de estados
- 🔵 Padrões de LED inteligente
- 🔵 Conexões BLE e coordenação de rádio
- 🔵 Buffer local e sincronização
- 🔵 Transferências de dados e configurações

## 🧪 Casos de Teste

### Teste de Inicialização
- Verifica boot sequence do hub e bici
- Carregamento de configs e estados
- Inicialização de serviços modulares

### Teste de Estados
- Transições de estado do hub
- Estados da bici (BOOT → CONFIG_REQUEST → SCANNING → AT_BASE)
- Coordenação de rádio WiFi/BLE

### Teste de Configuração
- Solicitação de config via BLE
- Aplicação de configuração dinâmica
- Persistência em LittleFS

### Teste de Conectividade
- Descoberta BLE entre bici e hub
- Handshake de conexão
- Manutenção de sessão

### Teste de Dados
- Buffer local de scans WiFi
- Sincronização via estados WIFI_SYNC
- Upload em lotes para Firebase
- Persistência de métricas

### Teste de Alertas
- Bateria baixa e transições de energia
- Desconexões e reconexões
- Timeouts e deep sleep

## 🔍 Debug

Para ver o estado completo do Firebase Mock:
```javascript
emulator.firebase.showData();
```

## 🎯 Benefícios

- ✅ **Teste offline** - Sem dependência de Firebase real
- ✅ **Desenvolvimento rápido** - Ciclos de teste instantâneos  
- ✅ **Debug visual** - Logs coloridos e detalhados
- ✅ **Cenários controlados** - Situações específicas reproduzíveis
- ✅ **Validação de fluxos** - Testa integração completa hub+bici
- ✅ **Prototipagem** - Experimenta mudanças sem hardware
- ✅ **Estados simulados** - Testa máquinas de estado complexas
- ✅ **Configuração dinâmica** - Valida troca de configs via BLE
- ✅ **Coordenação de rádio** - Simula interferência WiFi/BLE