# Fluxo da Lógica - WiFi Range Scanner

## Visão Geral do Sistema

O sistema opera em dois modos principais:
- **Modo Configuração**: Interface web para configurar o dispositivo
- **Modo Scanner**: Coleta contínua de dados WiFi

---

## 🔄 Fluxograma Principal

```
┌─────────────────┐
│     INÍCIO      │
│   (setup())     │
└─────────┬───────┘
          │
          ▼
┌─────────────────┐
│ Inicializar     │
│ - Serial        │
│ - LED           │
│ - Sistema Arq.  │
└─────────┬───────┘
          │
          ▼
┌─────────────────┐
│ Carregar Config │
│ loadConfig()    │
└─────────┬───────┘
          │
          ▼
┌─────────────────┐
│ Verificar Modo  │
│ de Inicialização│
└─────────┬───────┘
          │
          ▼
    ┌─────────────┐
    │ Botão FLASH │ ──── SIM ──┐
    │ pressionado?│            │
    └─────────────┘            │
          │ NÃO                │
          ▼                    │
    ┌─────────────┐            │
    │ Base WiFi   │ ──── SIM ──┤
    │ detectada?  │            │
    └─────────────┘            │
          │ NÃO                │
          ▼                    ▼
┌─────────────────┐    ┌─────────────────┐
│   MODO SCANNER  │    │ MODO CONFIGURAÇÃO│
│   (loop())      │    │ startConfigMode()│
└─────────┬───────┘    └─────────────────┘
          │                    │
          ▼                    ▼
┌─────────────────┐    ┌─────────────────┐
│ Loop Principal  │    │ Servidor Web    │
│ de Coleta       │    │ Ativo           │
└─────────────────┘    └─────────────────┘
```

---

## 📋 Detalhamento do Setup()

```
setup()
├── 1. Inicialização Hardware
│   ├── Serial.begin(115200)
│   ├── pinMode(LED_BUILTIN, OUTPUT)
│   └── delay(2000) // Tempo para botão FLASH
│
├── 2. Sistema de Arquivos
│   ├── LittleFS.begin()
│   └── Se falhar → LittleFS.format()
│
├── 3. Carregar Configurações
│   ├── loadConfig()
│   ├── Ler /bike.txt → config.bikeId
│   ├── Ler /timing.txt → tempos de scan
│   ├── Ler /bases.txt → SSIDs e senhas
│   └── Ler /firebase.txt → URL e chave
│
└── 4. Decisão de Modo
    ├── digitalRead(0) == LOW? → MODO CONFIG
    ├── checkAtBase()? → MODO CONFIG
    └── Senão → MODO SCANNER
```

---

## 🔄 Loop Principal (Modo Scanner)

```
loop()
├── 1. updateLED() // Sempre atualizar LED
│
├── 2. Verificar Modo
│   └── Se configMode → server.handleClient() → return
│
├── 3. Menu Serial
│   ├── Serial.available()?
│   ├── Se 'm' → showMenu() → handleSerialMenu()
│   └── Senão → limpar buffer
│
├── 4. Coleta de Dados
│   ├── scanWiFiNetworks()
│   └── storeData()
│
├── 5. Verificar Base
│   └── config.isAtBase = checkAtBase()
│
├── 6. Upload Condicional
│   ├── Se (isAtBase && dataCount > 0)
│   ├── connectToBase()
│   ├── syncTime()
│   └── uploadData()
│
├── 7. Status e Delay
│   ├── printStoredNetworks()
│   ├── Calcular delay (base vs movimento)
│   └── delay(scanDelay)
│
└── 8. Repetir Loop
```

---

## 📡 Fluxo de Coleta de Dados

```
scanWiFiNetworks()
├── WiFi.scanNetworks()
├── Para cada rede encontrada:
│   ├── Extrair SSID, BSSID, RSSI, Canal
│   ├── Armazenar em networks[i]
│   └── Incrementar networkCount
└── Máximo 30 redes

storeData()
├── Criar filename: "/scan_" + millis() + ".json"
├── Montar JSON compacto:
│   ├── [timestamp, realTime, battery, charging,
│   └── [[ssid,bssid,rssi,channel], ...]]
├── Salvar até 5 redes mais fortes
└── dataCount++
```

---

## 🏠 Detecção e Conexão com Base

```
checkAtBase()
├── Para cada rede escaneada:
│   ├── Comparar com config.baseSSID1/2/3
│   ├── Se encontrada e RSSI > -80dBm
│   └── return true
└── return false

connectToBase()
├── Para cada rede escaneada:
│   ├── getBasePassword(ssid)
│   ├── Se senha encontrada:
│   │   ├── WiFi.begin(ssid, password)
│   │   ├── Aguardar conexão (max 20 tentativas)
│   │   └── Se conectado → return true
│   └── Próxima rede
└── return false
```

---

## ☁️ Fluxo de Upload Firebase

```
uploadData()
├── Verificar configuração Firebase
├── Montar payload JSON mínimo
├── Configurar cliente HTTPS
├── Extrair host da URL
├── Conectar Firebase (porta 443)
├── Enviar requisição PUT
├── Aguardar resposta
├── Se sucesso (200 OK):
│   ├── dataCount = 0
│   └── Serial: "Upload OK!"
└── WiFi.disconnect()
```

---

## 🌐 Modo Configuração

```
startConfigMode()
├── configMode = true
├── Verificar origem:
│   ├── Se botão FLASH:
│   │   ├── WiFi.mode(WIFI_AP)
│   │   ├── WiFi.softAP("Bike-" + bikeId, "12345678")
│   │   └── IP: 192.168.4.1
│   └── Se base detectada:
│       ├── connectToBase()
│       └── IP: WiFi.localIP()
├── Configurar rotas web:
│   ├── "/" → handleRoot()
│   ├── "/config" → handleConfig()
│   ├── "/save" → handleSave()
│   ├── "/wifi" → handleWifi()
│   └── "/dados" → handleDados()
└── server.begin()

Loop em Modo Config:
└── server.handleClient() // Processar requisições web
```

---

## 💡 Padrões de LED

```
updateLED()
├── Modo Configuração:
│   └── 3 piscadas rápidas + pausa longa
│       └── [100ms ON, 100ms OFF] x3 + 1000ms OFF
│
├── Conectado na Base:
│   └── 1 piscada lenta + pausa longa
│       └── 500ms ON, 500ms OFF, 1500ms OFF
│
└── Coletando Dados:
    └── 2 piscadas rápidas + pausa média
        └── [200ms ON, 200ms OFF] x2 + 800ms OFF
```

---

## 📱 Menu Serial Interativo

```
handleSerialMenu()
├── 1) Monitorar redes → scanWiFiNetworks() + listar
├── 2) Verificar base → checkAtBase() + connectToBase()
├── 3) Testar Firebase → connectToBase() + uploadData()
├── 4) Mostrar config → imprimir todas configurações
├── 5) Ver dados salvos → listar arquivos scan_*.json
├── 6) Transferir dados → exportar JSON para backup
├── 7) Ativar modo AP → ESP.restart()
└── q) Sair do menu → voltar ao loop normal
```

---

## 🔧 Estados do Sistema

### Estado 1: Inicialização
- Carregamento de configurações
- Verificação de modo de operação
- Decisão entre Scanner ou Configuração

### Estado 2: Modo Scanner
- Coleta contínua de dados WiFi
- Detecção automática de bases
- Upload automático quando próximo da base
- Tempos diferentes: movimento (5s) vs base (30s)

### Estado 3: Modo Configuração
- Interface web ativa
- Permite alterar todas as configurações
- Visualização de dados coletados
- Monitoramento em tempo real

### Estado 4: Upload de Dados
- Conexão automática com base WiFi
- Sincronização de horário via NTP
- Upload seguro para Firebase
- Limpeza de dados após sucesso

---

## 🔄 Ciclo de Vida dos Dados

```
Coleta → Armazenamento → Upload → Limpeza
  ↓           ↓            ↓         ↓
Scan     JSON Local   Firebase   Delete
WiFi     (LittleFS)   (HTTPS)    Local
```

---

## ⚡ Pontos Críticos

1. **Detecção de Base**: RSSI > -80dBm para ativar modo base
2. **Buffer de Dados**: Máximo 20 registros em memória
3. **Timeout de Conexão**: 20 tentativas x 500ms = 10s máximo
4. **Formato Compacto**: JSON otimizado para economizar espaço
5. **Fallback Seguro**: Configurações padrão se arquivos não existirem

---

## 🛠️ Fluxo de Configuração

```
Interface Web → Formulário → handleSave() → saveConfig() → ESP.restart()
     ↓              ↓            ↓             ↓              ↓
Página HTML → POST /save → Validar → Salvar Arquivos → Reiniciar
```

Este fluxograma mostra como o sistema opera de forma autônoma, alternando entre coleta de dados e upload automático, com interface de configuração acessível tanto por botão físico quanto por proximidade de base WiFi.