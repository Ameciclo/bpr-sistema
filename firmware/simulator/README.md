# 🧪 BPR Bike Simulator

Simulador de bicicleta para testar a comunicação BLE com a central BPR.

## 🚀 Como usar

### 1. Compilar e fazer upload
```bash
cd firmware/simulator
pio run -t upload
```

### 2. Monitorar
```bash
pio device monitor
```

### 3. Comandos disponíveis
- `1` - Teste de conexão BLE
- `2` - Teste de bateria baixa  
- `3` - Teste multi-bicicleta
- `0` - Desconectar

## 🔧 Configuração

O simulador procura por uma central BPR com o nome "BPR Base Station" e se conecta automaticamente.

### Características BLE testadas:
- **Conexão/Desconexão**
- **Envio de ID da bicicleta**
- **Atualização de nível de bateria**
- **Simulação de atividade contínua**

## 📋 Testes disponíveis

### Teste de Conexão BLE (Comando 1)
- Procura pela central BPR
- Conecta e envia ID da bicicleta
- Simula atividade por 30 segundos
- Desconecta automaticamente

### Teste de Bateria Baixa (Comando 2)  
- Conecta à central
- Envia alerta de bateria baixa (3.2V)
- Mantém conexão por 20 segundos

### Teste Multi-bicicleta (Comando 3)
- Instrução para usar múltiplos ESP32s
- Cada dispositivo simula uma bicicleta diferente

## 🔍 Monitoramento

O simulador exibe logs detalhados:
- 🔵 Conexões estabelecidas
- 🔴 Desconexões  
- 📝 Dados enviados
- 🔋 Atualizações de bateria
- ❌ Erros e timeouts

## ⚙️ Hardware suportado

- **ESP32-C3** (Seeed XIAO ESP32C3)
- Outros ESP32 com BLE (ajustar board no platformio.ini)