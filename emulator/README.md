# 🧪 BPR Sistema Emulador

Emulador completo do sistema BPR que simula o comportamento das centrais, bicicletas e Firebase de forma offline.

## 🚀 Como usar

```bash
cd emulator
npm install
npm start
```

## 🎯 Cenários Disponíveis

### 🏢 Central inicializando e configurando
- Simula boot da central
- Carregamento de configurações do Firebase
- Inicialização do BLE
- Sistema de LED inteligente

### 🚲 Bike conectando na central
- Boot da bicicleta
- Descoberta e conexão BLE com central
- Envio de heartbeat
- Atualização de status

### 🔄 Fluxo completo: Central + Bike + Viagem
- Central inicializada
- Bike conecta
- Bike sai da base (viagem)
- Scans WiFi durante movimento
- Retorno à base
- Sincronização de dados

### 🔋 Teste de bateria baixa
- Simula bike com bateria baixa
- Envio de alerta
- Processamento pela central
- Notificação no sistema

### 📡 Múltiplas bikes simultâneas
- 3 bikes conectando simultaneamente
- Atividade paralela
- Gerenciamento de múltiplas conexões
- LED de contagem

## 🔧 Arquitetura

### Classes Principais

#### `BPREmulator`
- Orquestra os cenários
- Gerencia central e bikes
- Interface com usuário

#### `Central`
- Simula firmware da central ESP32
- Sistema de LED inteligente
- Gerenciamento BLE
- Heartbeat automático
- Sincronização Firebase

#### `Bike`
- Simula firmware da bicicleta ESP8266/ESP32
- Scans WiFi
- Tracking de viagens
- Gerenciamento de bateria
- Conexão BLE

#### `MockFirebase`
- Simula Firebase Realtime Database
- Estrutura de dados completa
- Operações CRUD
- Logs detalhados

## 📊 Dados Simulados

### Configurações
- Configurações globais do sistema
- Configurações específicas por central
- Parâmetros de LED, WiFi, BLE

### Dados Operacionais
- Status das bases e bikes
- Scans WiFi com redes fictícias
- Viagens com rotas e métricas
- Alertas de sistema
- Heartbeats automáticos

### Métricas
- Voltagem de bateria realística
- Posições GPS simuladas
- Consumo de CO2 calculado
- Distâncias percorridas

## 🎮 Interação

O emulador mostra em tempo real:
- 🔵 Logs da central (azul)
- 🔵 Logs das bikes (ciano)  
- 🔵 Logs do Firebase (cinza)
- 🔵 Estados do LED
- 🔵 Conexões BLE
- 🔵 Transferências de dados

## 🧪 Casos de Teste

### Teste de Inicialização
- Verifica boot sequence
- Carregamento de configs
- Inicialização de serviços

### Teste de Conectividade
- Descoberta BLE
- Handshake de conexão
- Manutenção de sessão

### Teste de Dados
- Upload de scans WiFi
- Sincronização de viagens
- Persistência de métricas

### Teste de Alertas
- Bateria baixa
- Desconexões
- Timeouts

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
- ✅ **Validação de fluxos** - Testa integração completa
- ✅ **Prototipagem** - Experimenta mudanças sem hardware