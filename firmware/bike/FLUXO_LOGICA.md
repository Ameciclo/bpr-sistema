# Fluxo da Lógica - WiFi Range Scanner ESP32-C3 v2.0

## Visão Geral do Sistema

Sistema de monitoramento de redes WiFi para bicicletas com **configurações online via Firebase** e **modos de operação inteligentes**.

### Modos de Operação v2.0:
- **Modo STARTUP**: Sincronização de configurações e NTP inicial
- **Modo BASE**: Operação na base com sincronizações e uploads
- **Modo VIAGEM**: Coleta ativa de dados WiFi
- **Modo EMERGÊNCIA**: Interface web para configuração manual (fallback)

---

## 🔄 Fluxograma Principal

```mermaid
flowchart TD
    A["🚀 INÍCIO<br/>(setup())"] --> B["⚙️ Inicializar<br/>Serial + LED + LittleFS"]
    B --> C["📋 Carregar Config<br/>config.txt"]
    C --> D["🔍 Verificar Modo<br/>de Inicialização"]
    
    D --> E{"🔘 Botão FLASH<br/>pressionado?"}
    E -->|SIM| G["🌐 MODO AP<br/>Bike-{ID} / 12345678"]
    
    E -->|NÃO| F{"📡 Alguma das 3<br/>bases detectada?"}
    F -->|SIM| H["🌐 MODO CONFIG<br/>Via Base WiFi"]
    F -->|NÃO| I["📊 MODO SCANNER<br/>(loop())"]
    
    G --> J["🖥️ Servidor Web<br/>192.168.4.1"]
    H --> K["🖥️ Servidor Web<br/>IP da Base"]
    I --> L["📦 Contar Arquivos<br/>countScanFiles()"]
    L --> M["🔄 Loop Principal<br/>de Coleta"]
    
    style A fill:#e1f5fe
    style G fill:#ffcdd2
    style H fill:#fff3e0
    style I fill:#e8f5e8
    style J fill:#ffcdd2
    style K fill:#fff3e0
    style M fill:#e8f5e8
```

---

## 📋 Detalhamento do Setup()

```mermaid
flowchart TD
    A["🚀 setup()"] --> B["⚙️ Inicialização Hardware"]
    B --> B1["Serial.begin(115200)"]
    B1 --> B2["pinMode(LED_BUILTIN, OUTPUT)"]
    B2 --> B3["delay(2000) // Detectar FLASH"]
    
    B3 --> C["💾 Sistema de Arquivos"]
    C --> C1{"LittleFS.begin()"}
    C1 -->|Falha| C2["LittleFS.format()"]
    C1 -->|Sucesso| D
    C2 --> D
    
    D["📋 Carregar config.txt"] --> D1["BIKE_ID (ex: sl01)"]
    D1 --> D2["⏱️ SCAN_TIME_ACTIVE=5000ms"]
    D2 --> D3["⏱️ SCAN_TIME_INACTIVE=30000ms"]
    D3 --> D4["📡 BASE1/2/3_SSID + PASSWORD"]
    D4 --> D5["🔥 FIREBASE_URL + KEY"]
    D5 --> D6["🧹 CLEANUP_ENABLED (0/1)"]
    D6 --> D7["📊 BASE_PROXIMITY_RSSI=-80"]
    
    D7 --> E["🎯 Decisão de Modo"]
    E --> E1{"digitalRead(0) == LOW?"}
    E1 -->|SIM| F["🌐 MODO AP"]
    E1 -->|NÃO| E2{"Alguma das 3<br/>bases detectada?"}
    E2 -->|SIM| G["🌐 MODO CONFIG"]
    E2 -->|NÃO| H["📊 MODO SCANNER"]
    
    style A fill:#e1f5fe
    style F fill:#ffcdd2
    style G fill:#fff3e0
    style H fill:#e8f5e8
```

---

## 🔄 Loop Principal (Modo Scanner)

```mermaid
flowchart TD
    A["🔄 loop()"] --> B["💡 updateLED()<br/>Padrões visuais"]
    B --> B1["📦 Atualizar dataCount<br/>countScanFiles()"]
    B1 --> C{"🌐 configMode?"}
    C -->|SIM| D["🖥️ server.handleClient()"]
    D --> A
    
    C -->|NÃO| E{"📱 Serial.available()?"}
    E -->|'m'| F["📋 Menu Interativo (8 opções)"]
    E -->|'d'| F1["🔍 Diagnóstico Completo"]
    E -->|'t'| F2["🧪 Teste Armazenamento"]
    E -->|Outros| G["🧹 Limpar buffer"]
    F --> H
    F1 --> H
    F2 --> H
    G --> H
    
    H["📡 Coleta WiFi"] --> H1["scanWiFiNetworks()<br/>Até 30 redes"]
    H1 --> H2{"networkCount > 0?"}
    H2 -->|SIM| H3["storeData()<br/>Top 10 redes + bateria"]
    H2 -->|NÃO| H4["⚠️ Nenhuma rede"]
    H3 --> I
    H4 --> I
    
    I["🏠 Verificar Bases"] --> I1["checkAtBase()<br/>3 bases + RSSI > -80dBm"]
    
    I1 --> J{"isAtBase &&<br/>dataCount > 0?"}
    J -->|SIM| K["☁️ Upload Automático"]
    J -->|NÃO| L
    
    K --> K1["connectToBase()<br/>Primeira base disponível"]
    K1 --> K2["syncTime() via NTP"]
    K2 --> K3["uploadOptimizedData()<br/>Estrutura v2.0"]
    K3 --> K4{"Upload OK?"}
    K4 -->|SIM| K5["🧹 Cleanup opcional"]
    K4 -->|NÃO| L
    K5 --> L
    
    L["📊 Status Detalhado"] --> L1["=== Bike ID - X redes - Bat: Y% ==="]
    L1 --> L2{"Na base?"}
    L2 -->|SIM| L3["delay(30s) - Modo inativo"]
    L2 -->|NÃO| L4["delay(5s) - Modo ativo"]
    L3 --> A
    L4 --> A
    
    style A fill:#e8f5e8
    style K fill:#e1f5fe
    style D fill:#fff3e0
    style F1 fill:#fff9c4
    style F2 fill:#fff9c4
```

---

## 📡 Fluxo de Coleta de Dados

```mermaid
flowchart TD
    A["📡 scanWiFiNetworks()"] --> B["WiFi.scanNetworks()<br/>Escanear todas disponíveis"]
    B --> C["Para cada rede (max 30):"]
    C --> D["Extrair: SSID, BSSID, RSSI, Canal"]
    D --> E["Filtrar redes válidas"]
    E --> F["Armazenar em networks[i]"]
    F --> G["Incrementar networkCount"]
    
    G --> H["💾 storeData()"]
    H --> H1{"networkCount > 0?"}
    H1 -->|NÃO| H2["⚠️ Pular - nenhuma rede"]
    H1 -->|SIM| I["Criar: /scan_millis().json"]
    
    I --> J["📊 Montar JSON compacto:"]
    J --> K["timestamp + realTime + batteryLevel"]
    K --> L["+ arrays de redes WiFi"]
    L --> M["Selecionar top 10 redes"]
    M --> N["File.print(jsonData)"]
    N --> O{"Escrita OK?"}
    O -->|SIM| P["dataCount++ ✅"]
    O -->|NÃO| Q["❌ Log erro de escrita"]
    
    P --> R["📈 trackBattery()"]
    Q --> R
    H2 --> R
    R --> S["Registrar nível + status carregamento"]
    
    style A fill:#e8f5e8
    style H fill:#e1f5fe
    style P fill:#c8e6c9
    style Q fill:#ffcdd2
```

---

## 🏠 Detecção e Conexão com Base (Suporte a 3 Bases)

```mermaid
flowchart TD
    A["🔍 checkAtBase()"] --> B["Para cada rede escaneada:"]
    B --> C{"SSID == BASE1/2/3?"}
    C -->|NÃO| D["Próxima rede"]
    C -->|SIM| E{"RSSI > -80dBm?"}
    E -->|NÃO| D
    E -->|SIM| F["✅ Base detectada!"]
    F --> G["return true"]
    D --> H{"Mais redes?"}
    H -->|SIM| B
    H -->|NÃO| I["return false"]
    
    J["🔌 connectToBase()"] --> K["Para cada base configurada:"]
    K --> L["WiFi.begin(ssid, password)"]
    L --> M["Aguardar conexão (20 tentativas)"]
    M --> N{"Conectado?"}
    N -->|SIM| O["📝 Registrar evento conexão"]
    O --> P["✅ return true"]
    N -->|NÃO| Q{"Próxima base?"}
    Q -->|SIM| K
    Q -->|NÃO| R["❌ return false"]
    
    style F fill:#c8e6c9
    style P fill:#c8e6c9
    style I fill:#ffcdd2
    style R fill:#ffcdd2
```

### Configuração das Bases:
- **BASE1**: WiFi-Estacao-Central / senha123
- **BASE2**: WiFi-Oficina / senha456  
- **BASE3**: WiFi-Deposito / senha789
- **Proximidade**: RSSI > -80dBm
- **Conexão**: Primeira base disponível

---

## ☁️ Fluxo de Upload Firebase (Estrutura Otimizada v2.0)

```mermaid
flowchart TD
    A["☁️ uploadOptimizedData()"] --> B{"Firebase configurado?"}
    B -->|NÃO| B1["⚠️ Cancelar - sem config"]
    B -->|SIM| C{"dataCount > 0?"}
    C -->|NÃO| C1["⚠️ Nenhum dado para enviar"]
    C -->|SIM| D["📦 buildOptimizedPayload()"]
    
    D --> D1["🔍 Listar arquivos scan_*.json"]
    D1 --> D2["📖 Ler cada arquivo"]
    D2 --> D3["🔄 Agrupar por sessão"]
    D3 --> D4["📊 Estrutura otimizada:"]
    D4 --> D5["sessions/scans/battery/connections"]
    D5 --> D6{"Payload válido?"}
    D6 -->|NÃO| D7["❌ Payload vazio"]
    D6 -->|SIM| E["🌐 Conectar Firebase"]
    
    E --> E1["generateSessionId()<br/>YYYYMMDD_XXX"]
    E1 --> E2["WiFiClientSecure.connect()"]
    E2 --> E3{"SSL conectado?"}
    E3 -->|NÃO| E4["❌ Falha SSL"]
    E3 -->|SIM| F["📤 HTTP PUT Request"]
    
    F --> F1["PUT /bikes/{BIKE_ID}/sessions/{SESSION}.json"]
    F1 --> F2["Content-Type: application/json"]
    F2 --> F3["Aguardar resposta (10s)"]
    F3 --> F4{"Status 200?"}
    F4 -->|NÃO| F5["❌ Erro HTTP"]
    F4 -->|SIM| G["✅ Upload Sucesso!"]
    
    G --> G1{"CLEANUP_ENABLED=1?"}
    G1 -->|SIM| G2["🧹 Remover scan_*.json"]
    G1 -->|NÃO| G3["📚 Manter arquivos locais"]
    G2 --> H["📊 Atualizar uploadStatus"]
    G3 --> H
    H --> I["🔌 WiFi.disconnect()"]
    I --> J["📈 Redução 60-70% dados"]
    
    style A fill:#e1f5fe
    style G fill:#c8e6c9
    style J fill:#c8e6c9
    style B1 fill:#ffcdd2
    style C1 fill:#fff3e0
    style D7 fill:#ffcdd2
    style E4 fill:#ffcdd2
    style F5 fill:#ffcdd2
```

### Estrutura Firebase Otimizada:
```json
{
  "bikes": {
    "sl01": {
      "sessions": {
        "20241201_001": {
          "start": 1760209736,
          "end": 1760210131,
          "scans": [[timestamp, [["SSID","BSSID",rssi,ch]]]],
          "battery": [[timestamp, level]],
          "connections": [[timestamp, "event", "ssid", "ip"]]
        }
      },
      "networks": {
        "aa:bb:cc:dd:ee:ff": {"ssid": "VALENCA1", "first": 1760209736}
      }
    }
  }
}
```

---

## 🌐 Modo Configuração

```mermaid
flowchart TD
    A["🌐 startConfigMode()"] --> B["configMode = true"]
    B --> C{"Origem da ativação?"}
    
    C -->|"Botão FLASH"| D["📡 Modo Access Point"]
    D --> D1["WiFi.mode(WIFI_AP)"]
    D1 --> D2["WiFi.softAP('Bike-sl01', '12345678')"]
    D2 --> D3["IP: 192.168.4.1"]
    D3 --> D4["LED: 3 piscadas rápidas"]
    
    C -->|"Base detectada"| E["🔌 Conectar à Base"]
    E --> E1["connectToBase()"]
    E1 --> E2["IP: WiFi.localIP()"]
    E2 --> E3["LED: 1 piscada lenta"]
    
    D4 --> F["🖥️ Configurar Servidor Web"]
    E3 --> F
    F --> F1["/ - Página inicial"]
    F1 --> F2["/config - Configurações"]
    F2 --> F3["/save - Salvar alterações"]
    F3 --> F4["/wifi - Monitorar WiFi"]
    F4 --> F5["/dados - Ver arquivos"]
    F5 --> G["server.begin()"]
    
    G --> H["🔄 Loop Configuração"]
    H --> I["server.handleClient()"]
    I --> J["Processar requisições HTTP"]
    J --> H
    
    style D fill:#ffcdd2
    style E fill:#fff3e0
    style F fill:#e1f5fe
```

### Interface Web:
- **Página Inicial**: Links para todas as funcionalidades
- **Configurações**: Editar config.txt via formulário
- **Ver WiFi**: Redes detectadas em tempo real
- **Ver Dados**: Arquivos salvos + conteúdo
- **Segurança**: Alterações preservam dados coletados

---

## 💡 Padrões de LED

```mermaid
flowchart TD
    A["💡 updateLED()"] --> B{"Estado do Sistema"}
    
    B -->|"configMode"| C["🔴 Modo Configuração"]
    C --> C1["3 piscadas rápidas + pausa"]
    C1 --> C2["[100ms ON, 100ms OFF] x3"]
    C2 --> C3["+ 1000ms OFF"]
    
    B -->|"isAtBase"| D["🟢 Conectado na Base"]
    D --> D1["1 piscada lenta + pausa"]
    D1 --> D2["500ms ON, 500ms OFF"]
    D2 --> D3["+ 1500ms OFF"]
    
    B -->|"normal"| E["🟡 Coletando Dados"]
    E --> E1["2 piscadas rápidas + pausa"]
    E1 --> E2["[200ms ON, 200ms OFF] x2"]
    E2 --> E3["+ 800ms OFF"]
    
    style C fill:#ffcdd2
    style D fill:#c8e6c9
    style E fill:#fff3e0
```

### Significado dos LEDs:
- 🔴 **3 piscadas**: Modo AP ou conectado à base para configuração
- 🟡 **2 piscadas**: Operação normal, coletando dados
- 🟢 **1 piscada**: Próximo da base, pronto para upload

### Estados Visuais:
- **Configuração**: Fácil identificação para setup inicial
- **Coleta**: Indica funcionamento normal
- **Base**: Confirma detecção e possibilidade de upload

---

## 📱 Menu Serial Interativo

```mermaid
flowchart TD
    A["📱 Serial Input"] --> B{"Comando?"}
    
    B -->|'m'| C["📋 Menu Principal"]
    B -->|'d'| D["🔍 Diagnóstico Completo"]
    B -->|'t'| E["🧪 Teste Armazenamento"]
    
    C --> C1["1️⃣ Monitorar redes WiFi"]
    C --> C2["2️⃣ Verificar conexão com base"]
    C --> C3["3️⃣ Testar conexão Firebase"]
    C --> C4["4️⃣ Mostrar configurações"]
    C --> C5["5️⃣ Ver dados salvos"]
    C --> C6["6️⃣ Transferir dados (backup)"]
    C --> C7["7️⃣ Ativar modo AP"]
    C --> C8["q️⃣ Sair do menu"]
    
    D --> D1["📁 LittleFS info + arquivos"]
    D --> D2["⚙️ Todas as configurações"]
    D --> D3["📶 Status WiFi atual"]
    D --> D4["🔍 Teste scan completo"]
    D --> D5["💾 Teste escrita/leitura"]
    D --> D6["🔢 Variáveis globais"]
    D --> D7["🔋 Status bateria"]
    
    E --> E1["📡 Executar scan real"]
    E --> E2["💾 Chamar storeData()"]
    E --> E3["📂 Listar arquivos criados"]
    E --> E4["📝 Mostrar conteúdo JSON"]
    E --> E5["✅ Validar integridade"]
    
    style D fill:#fff9c4
    style E fill:#fff9c4
```

### Comandos Rápidos:
- **m**: Menu completo com 8 opções
- **d**: Diagnóstico detalhado do sistema
- **t**: Teste de funcionalidade de armazenamento

### Funcionalidades do Menu:
1. **Monitoramento**: Scan contínuo em tempo real
2. **Conectividade**: Teste das 3 bases configuradas
3. **Firebase**: Validação de upload
4. **Configuração**: Visualizar config.txt
5. **Dados**: Listar e examinar arquivos
6. **Backup**: Exportar dados entre INICIO/FIM
7. **AP Mode**: Forçar modo configuração
8. **Sair**: Retornar ao loop normal

---

## 🔧 Estados do Sistema

### Estado 1: Inicialização 🚀
- **Hardware**: Serial, LED, LittleFS
- **Configuração**: Carregar config.txt (BIKE_ID, bases, Firebase)
- **Decisão**: Botão FLASH → AP | Base detectada → Config | Normal → Scanner
- **Validação**: Verificar integridade das configurações

### Estado 2: Modo Scanner 📊
- **Coleta**: Scan WiFi a cada 5s (ativo) ou 30s (base)
- **Armazenamento**: Top 10 redes + bateria em JSON compacto
- **Detecção**: 3 bases simultâneas (RSSI > -80dBm)
- **Upload**: Automático quando próximo + dados disponíveis
- **LED**: 2 piscadas (normal) ou 1 piscada (base)

### Estado 3: Modo Configuração 🌐
- **AP Mode**: Bike-{ID} / 12345678 (192.168.4.1)
- **Base Mode**: Conectado à base WiFi (IP dinâmico)
- **Interface**: 5 páginas web (config, wifi, dados, etc.)
- **Segurança**: Alterações preservam dados coletados
- **LED**: 3 piscadas rápidas

### Estado 4: Upload Otimizado ☁️
- **Estrutura v2.0**: 60-70% redução de dados
- **Sessões**: Agrupamento temporal inteligente
- **Normalização**: Redes únicas + referências
- **Cleanup**: Opcional após upload bem-sucedido
- **Histórico**: Manter últimos uploads (configurável)

### Estado 5: Diagnóstico 🔍
- **Menu Serial**: 8 opções interativas
- **Testes**: Armazenamento, conectividade, Firebase
- **Backup**: Exportação manual de dados
- **Monitoramento**: Status em tempo real

---

## 📈 Melhorias Implementadas

### 🚀 Estrutura Otimizada (v2.0)
- **Redução**: 60-70% no tamanho dos dados Firebase
- **Capacidade**: 10 redes WiFi por scan (antes: 5)
- **Agrupamento**: Sessões por período de coleta
- **Compactação**: Arrays eliminam redundâncias
- **Normalização**: Histórico de redes descobertas

### 🔧 Funcionalidades Avançadas
- **3 Bases**: Suporte simultâneo com fallback automático
- **Bateria**: Monitoramento + status de carregamento
- **Diagnóstico**: Menu interativo completo
- **Backup**: Exportação manual segura
- **Interface**: Web responsiva para configuração

### 📊 Benefícios Operacionais
- **Performance**: Uploads mais rápidos e econômicos
- **Análise**: Consultas Firebase otimizadas
- **Mobilidade**: Melhor detecção de padrões
- **Manutenção**: Diagnóstico integrado
- **Flexibilidade**: Configuração sem perda de dadoso)
- Conexão automática com qualquer das 3 bases WiFi
- Sincronização de horário via NTP
- Agrupamento de dados em sessões temporais
- Upload compacto para Firebase (60-70% redução)
- Limpeza condicional (CLEANUP_ENABLED)
- Manutenção de histórico (MAX_UPLOADS_HISTORY)

---

## 🔄 Ciclo de Vida dos Dados (Otimizado v2.0)

```mermaid
flowchart LR
    A["📡 Coleta<br/>10 redes WiFi<br/>+ Battery"] --> B["💾 Armazenamento<br/>JSON Local<br/>(LittleFS)"]
    
    B --> C["📊 Agrupamento<br/>Sessões Temporais<br/>Correlação"]
    
    C --> D["☁️ Upload<br/>Firebase Compacto<br/>60-70% menor"]
    
    D --> E{"CLEANUP_ENABLED?"}
    E -->|SIM| F["🧹 Delete Local"]
    E -->|NÃO| G["📚 Manter Histórico<br/>MAX_UPLOADS"]
    
    F --> H["🔄 Próximo Ciclo"]
    G --> H
    
    subgraph "Estrutura Otimizada"
        I["sessions/[id]/<br/>├── scans: [[ts, networks]]<br/>├── battery: [[ts, level]]<br/>└── connections: [[ts, event]]"]
        J["networks/[bssid]/<br/>├── ssid<br/>└── first_seen"]
    end
    
    style A fill:#e8f5e8
    style D fill:#e1f5fe
    style F fill:#ffebee
    style G fill:#fff3e0
```

---

## 🔧 Comandos de Debug Rápido

| Comando | Função | Quando Usar |
|---------|--------|-------------|
| `d` | Diagnóstico completo | Verificar status geral |
| `t` | Teste armazenamento | Problemas com dados |
| `m` | Menu interativo | Configurações avançadas |

---

## ⚡ Pontos Críticos

1. **Detecção de Base**: RSSI > BASE_PROXIMITY_RSSI (-80dBm) para ativar modo base
2. **Múltiplas Bases**: Suporte a até 3 bases WiFi configuradas
3. **Buffer Otimizado**: 10 redes WiFi por scan (antes: 5)
4. **Timeout de Conexão**: 20 tentativas x 500ms = 10s máximo
5. **Estrutura Compacta**: Sessões agrupadas, 60-70% redução no Firebase
6. **Limpeza Inteligente**: Configurável via CLEANUP_ENABLED
7. **Histórico Controlado**: MAX_UPLOADS_HISTORY limita dados mantidos
8. **Fallback Seguro**: Configurações padrão se arquivos não existirem

---

## 🛠️ Fluxo de Configuração

```mermaid
flowchart TD
    A["🌐 Interface Web"] --> A1{"Modo de Acesso?"}
    A1 -->|Botão FLASH| A2["📡 AP: Bike-ID<br/>192.168.4.1"]
    A1 -->|Base detectada| A3["🏠 IP da base<br/>ex: 192.168.252.213"]
    
    A2 --> B["📝 Formulário Configurações"]
    A3 --> B
    
    B --> B1["⚙️ Bike ID + Modo coleta"]
    B1 --> B2["📡 3 Bases WiFi"]
    B2 --> B3["🔥 Firebase URL + Key"]
    B3 --> B4["🧹 Cleanup + Proximidade"]
    
    B4 --> C["📤 POST /save"]
    C --> D["✅ Validar dados"]
    D --> E["💾 saveConfig()"]
    E --> F["📄 Salvar /config.txt"]
    F --> G["🔄 ESP.restart()"]
    
    subgraph "Rotas Web"
        H["/config - Formulário<br/>/dados - Ver arquivos<br/>/wifi - Scan tempo real<br/>/test - Testar Firebase"]
    end
    
    B -.-> H
    
    style A2 fill:#fff3e0
    style A3 fill:#e8f5e8
    style G fill:#e1f5fe
```

## 🛠️ Ferramentas de Diagnóstico

```mermaid
flowchart TD
    A["🔍 Diagnóstico ('d')"] --> B["📁 Sistema de Arquivos"]
    A --> C["⚙️ Configurações"]
    A --> D["📶 Status WiFi"]
    A --> E["🔍 Teste de Scan"]
    A --> F["💾 Teste Escrita/Leitura"]
    A --> G["🔢 Variáveis Globais"]
    
    B --> B1["✅ LittleFS montado"]
    B --> B2["📂 Lista todos arquivos"]
    B --> B3["💾 Espaço usado/total"]
    
    C --> C1["🆔 Bike ID"]
    C --> C2["📡 3 Bases configuradas"]
    C --> C3["🔥 Firebase status"]
    C --> C4["⏱️ Timings"]
    
    D --> D1["📊 Status conexão"]
    D --> D2["🌐 IP atual"]
    D --> D3["📡 SSID conectado"]
    D --> D4["📶 RSSI atual"]
    
    H["🧪 Teste Armazenamento ('t')"] --> I["📡 Scan real"]
    I --> J["💾 Chamar storeData()"]
    J --> K["📊 Verificar dataCount"]
    K --> L["📂 Listar arquivos criados"]
    L --> M["📝 Mostrar conteúdo"]
    
    style A fill:#fff9c4
    style H fill:#fff9c4
```

## 📈 Melhorias v2.0 + Correções

```mermaid
flowchart TD
    A["📊 Versão Original"] --> B["🚀 Versão Atual v2.0+"]
    
    subgraph "Problemas Corrigidos"
        C["❌ storeData() sem verificação"]
        D["❌ Upload payload vazio"]
        E["❌ dataCount incorreto"]
        F["❌ Interface web sem dados"]
        G["❌ Logging insuficiente"]
    end
    
    subgraph "Melhorias Implementadas"
        H["✅ Verificação networkCount > 0"]
        I["✅ Debug detalhado upload"]
        J["✅ countScanFiles() real"]
        K["✅ Interface web corrigida"]
        L["✅ Logging completo"]
        M["✅ Ferramentas diagnóstico"]
        N["✅ 10 redes por scan"]
        O["✅ 3 bases WiFi"]
        P["✅ Cleanup configurável"]
    end
    
    A --> C
    A --> D
    A --> E
    A --> F
    A --> G
    
    B --> H
    B --> I
    B --> J
    B --> K
    B --> L
    B --> M
    B --> N
    B --> O
    B --> P
    
    style A fill:#ffebee
    style B fill:#e8f5e8
```

## 🎯 Fluxo de Resolução de Problemas

```mermaid
flowchart TD
    A["⚠️ Problema Detectado"] --> B["🔍 Digite 'd' para diagnóstico"]
    B --> C{"Sistema de arquivos OK?"}
    C -->|❌| C1["Reformatar LittleFS"]
    C -->|✅| D{"Configurações OK?"}
    
    D -->|❌| D1["Verificar /config.txt"]
    D -->|✅| E{"WiFi funcionando?"}
    
    E -->|❌| E1["Verificar bases WiFi"]
    E -->|✅| F{"Dados sendo salvos?"}
    
    F -->|❌| F1["🧪 Digite 't' para teste"]
    F -->|✅| G{"Upload funcionando?"}
    
    G -->|❌| G1["Verificar Firebase config"]
    G -->|✅| H["✅ Sistema OK"]
    
    F1 --> F2["Verificar networkCount"]
    F2 --> F3["Verificar storeData()"]
    F3 --> F4["Verificar espaço disco"]
    
    style A fill:#ffcdd2
    style H fill:#c8e6c9
    style B fill:#fff9c4
    style F1 fill:#fff9c4
```

## 🏆 Resumo do Sistema

Este fluxograma mostra como o sistema opera de forma autônoma e robusta:

### 🔄 **Operação Normal:**
- Coleta contínua de dados WiFi (10 redes por scan)
- Detecção automática de proximidade (3 bases WiFi)
- Upload otimizado para Firebase (60-70% redução)
- Limpeza configurável de dados locais

### 🛠️ **Ferramentas de Debug:**
- **`d`** - Diagnóstico completo do sistema
- **`t`** - Teste específico de armazenamento
- **`m`** - Menu interativo completo

### 🌐 **Interface de Configuração:**
- Acessível por botão FLASH ou proximidade de base
- Configuração via web browser
- Visualização de dados coletados
- Teste de conectividade Firebase

### ✅ **Robustez:**
- Verificações de integridade em cada etapa
- Logging detalhado para troubleshooting
- Fallbacks seguros para configurações
- Recuperação automática de erros

O sistema está preparado para operação autônoma em bicicletas, com capacidade de diagnóstico e manutenção remota via interface web.