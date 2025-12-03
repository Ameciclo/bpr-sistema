# WiFi Range Scanner - Fluxos do Sistema

## 1. Fluxo Principal de Inicialização

```mermaid
flowchart TD
    A[Início] --> B[Inicializar Serial/LED]
    B --> C[Montar LittleFS]
    C --> D[Carregar Configurações]
    D --> E{Botão FLASH pressionado?}
    E -->|Sim| F[Modo Configuração Forçado]
    E -->|Não| G[Escanear WiFi]
    G --> H{Base WiFi detectada?}
    H -->|Sim| I[Modo Configuração]
    H -->|Não| J[Modo Scanner Normal]
    F --> K[Iniciar AP/WebServer]
    I --> L[Conectar à Base + WebServer]
    J --> M[Loop Principal Scanner]
    K --> N[Aguardar Configuração Web]
    L --> N
    N --> O[Salvar Config + Reiniciar]
    M --> P[Operação Contínua]
```

## 2. Loop Principal de Operação

```mermaid
flowchart TD
    A[Loop Principal] --> B[Atualizar LED Status]
    B --> C{Modo Configuração?}
    C -->|Sim| D[Processar WebServer]
    C -->|Não| E{Comando Serial 'm'?}
    E -->|Sim| F[Ativar Menu Serial]
    E -->|Não| G[Escanear Redes WiFi]
    F --> H[Processar Menu 30s]
    H --> G
    G --> I[Armazenar Dados Localmente]
    I --> J[Verificar Proximidade Base]
    J --> K{Próximo à Base?}
    K -->|Sim| L[Conectar à Base]
    K -->|Não| M[Aguardar Próximo Scan]
    L --> N{Conectado?}
    N -->|Sim| O[Sincronizar Horário NTP]
    N -->|Não| M
    O --> P{Dados para Upload?}
    P -->|Sim| Q[Upload para Firebase]
    P -->|Não| R[Upload Status Periódico]
    Q --> S{Upload OK?}
    S -->|Sim| T[Limpar Arquivos Locais]
    S -->|Não| U[Manter Dados Locais]
    T --> V[Desconectar Base]
    U --> V
    R --> V
    V --> M
    M --> W[Delay Configurável]
    W --> A
    D --> A
```

## 3. Sistema de Configuração

```mermaid
flowchart TD
    A[Carregar Configurações] --> B[Ler bike.txt]
    B --> C[Ler timing.txt]
    C --> D[Ler bases.txt]
    D --> E[Ler firebase.txt]
    E --> F[Configurações Carregadas]
    
    G[Salvar Configurações] --> H[Escrever bike.txt]
    H --> I[Escrever timing.txt]
    I --> J[Escrever bases.txt]
    J --> K[Escrever firebase.txt]
    K --> L[Configurações Salvas]
    
    M[Interface Web] --> N[Página Principal]
    N --> O[Configurações]
    N --> P[Ver WiFi]
    N --> Q[Ver Dados]
    O --> R[Formulário Config]
    R --> S[Salvar + Reiniciar]
    P --> T[Lista Redes Tempo Real]
    Q --> U[Arquivos JSON Locais]
```

## 4. Detecção e Conexão com Bases

```mermaid
flowchart TD
    A[Verificar Proximidade Base] --> B[Escanear Redes]
    B --> C{Base1 RSSI > -80?}
    C -->|Sim| D[Base Detectada]
    C -->|Não| E{Base2 RSSI > -80?}
    E -->|Sim| D
    E -->|Não| F{Base3 RSSI > -80?}
    F -->|Sim| D
    F -->|Não| G[Nenhuma Base Próxima]
    
    D --> H[Conectar à Base]
    H --> I[Obter Senha da Base]
    I --> J[WiFi begin]
    J --> K{Conectado em 10s?}
    K -->|Sim| L[Conexão Estabelecida]
    K -->|Não| M[Falha na Conexão]
    L --> N[Obter IP Gateway]
    N --> O[Registrar Conexão]
    M --> P[Tentar Próxima Base]
    G --> Q[Modo Movimento]
```

## 5. Coleta e Armazenamento de Dados

```mermaid
flowchart TD
    A[Escanear WiFi] --> B[WiFi scanNetworks]
    B --> C[Processar até 30 Redes]
    C --> D[Extrair SSID BSSID RSSI Canal]
    D --> E[Armazenar em Buffer]
    
    E --> F[Criar Arquivo JSON]
    F --> G[Formato timestamp realTime networks]
    G --> H[Salvar em scan timestamp json]
    H --> I[Incrementar Contador Dados]
    
    I --> J[Monitorar Bateria A0]
    J --> K[Calcular Percentual]
    K --> L[Registrar Status Bateria]
```

## 6. Upload para Firebase

```mermaid
flowchart TD
    A[Upload Firebase] --> B{Firebase Configurado?}
    B -->|Não| C[Pular Upload]
    B -->|Sim| D[Sincronizar NTP]
    D --> E[Criar Payload JSON]
    E --> F[Conectar Firebase HTTPS]
    F --> G{Conexão OK?}
    G -->|Não| H[Falha Upload]
    G -->|Sim| I[Enviar PUT Request]
    I --> J[Aguardar Resposta]
    J --> K{Status 200 OK?}
    K -->|Sim| L[Upload Sucesso]
    K -->|Não| M[Erro Upload]
    L --> N[Limpar Arquivos Locais]
    L --> O[Upload Status]
    M --> P[Manter Dados Locais]
    N --> Q[Desconectar WiFi]
    P --> Q
    H --> Q
    C --> Q
```

## 7. Sistema de Status LED

```mermaid
flowchart TD
    A[Atualizar LED] --> B{Modo Configuração?}
    B -->|Sim| C[3 Piscadas Rápidas + Pausa]
    B -->|Não| D{Conectado à Base?}
    D -->|Sim| E[1 Piscada Lenta + Pausa]
    D -->|Não| F[2 Piscadas Rápidas + Pausa]
    
    C --> G[LED Vermelho - Config]
    E --> H[LED Verde - Base]
    F --> I[LED Amarelo - Movimento]
    
    G --> J[Repetir Padrão]
    H --> J
    I --> J
```

## 8. Menu Serial Interativo

```mermaid
flowchart TD
    A[Comando 'm'] --> B[Mostrar Menu]
    B --> C[Aguardar Entrada 30s]
    C --> D{Opção Selecionada}
    D -->|1| E[Monitorar Redes]
    D -->|2| F[Verificar Conexão Base]
    D -->|3| G[Testar Firebase]
    D -->|4| H[Mostrar Configurações]
    D -->|5| I[Ver Dados Salvos]
    D -->|6| J[Transferir Dados]
    D -->|7| K[Ativar Modo AP]
    D -->|q| L[Sair Menu]
    D -->|timeout| M[Timeout - Sair]
    
    E --> N[Lista WiFi Tempo Real]
    F --> O[Teste Conectividade]
    G --> P[Teste Upload]
    H --> Q[Config Atual]
    I --> R[Arquivos JSON]
    J --> S[Export Backup]
    K --> T[Forçar Config Mode]
    
    N --> C
    O --> C
    P --> C
    Q --> C
    R --> C
    S --> C
    T --> U[Reiniciar em Config]
    L --> V[Voltar Loop Principal]
    M --> V
```

## 9. Estrutura de Dados

```mermaid
erDiagram
    Config {
        int scanTimeActive
        int scanTimeInactive
        char baseSSID1_32
        char basePassword1_32
        char baseSSID2_32
        char basePassword2_32
        char baseSSID3_32
        char basePassword3_32
        char bikeId_10
        bool isAtBase
        char firebaseUrl_128
        char firebaseKey_64
    }
    
    WiFiNetwork {
        char ssid_32
        char bssid_18
        int rssi
        int channel
        int encryption
    }
    
    ScanData {
        unsigned_long timestamp
        WiFiNetwork networks_10
        int networkCount
        float batteryLevel
        bool isCharging
    }
    
    ConnectionEvent {
        unsigned_long timestamp
        char baseSSID_32
        char ip_16
        bool connected
    }
    
    BatteryEvent {
        unsigned_long timestamp
        float percentage
    }
```

## 10. Arquivos do Sistema

```mermaid
flowchart TD
    A[Sistema de Arquivos LittleFS] --> B[Configurações]
    A --> C[Dados Coletados]
    A --> D[Status Logs]
    
    B --> E[bike txt ID da Bicicleta]
    B --> F[timing txt Tempos de Scan]
    B --> G[bases txt SSIDs e Senhas]
    B --> H[firebase txt URL e Chave]
    
    C --> I[scan timestamp json Dados WiFi]
    C --> J[Formato Compacto JSON]
    
    D --> K[connections log Eventos Conexão]
    D --> L[battery log Status Bateria]
```

## 11. Estados do Sistema

```mermaid
stateDiagram-v2
    [*] --> Inicializando
    Inicializando --> ConfigMode : Botão FLASH ou Base Detectada
    Inicializando --> ScannerMode : Operação Normal
    
    ConfigMode --> WebServer : Modo AP ou Conectado
    WebServer --> Configurando : Interface Web
    Configurando --> Reiniciando : Salvar Config
    Reiniciando --> [*]
    
    ScannerMode --> Movimento : Longe das Bases
    ScannerMode --> ProximoBase : Base Detectada
    
    Movimento --> Escaneando : Scan Periódico
    Escaneando --> ArmazenandoDados : Dados Coletados
    ArmazenandoDados --> Movimento : Dados Salvos
    
    ProximoBase --> ConectandoBase : Tentar Conexão
    ConectandoBase --> Conectado : Sucesso
    ConectandoBase --> Movimento : Falha
    
    Conectado --> SincronizandoTempo : NTP
    SincronizandoTempo --> UploadDados : Dados Disponíveis
    SincronizandoTempo --> UploadStatus : Sem Dados
    UploadDados --> LimpandoArquivos : Upload OK
    UploadDados --> ManterDados : Upload Falha
    LimpandoArquivos --> Desconectando
    ManterDados --> Desconectando
    UploadStatus --> Desconectando
    Desconectando --> Movimento
```

## 12. Fluxo de Dados Firebase

```mermaid
sequenceDiagram
    participant B as Bicicleta
    participant W as WiFi Base
    participant N as NTP Server
    participant F as Firebase
    
    B->>W: Detectar Base (RSSI > -80)
    B->>W: Conectar WiFi
    W-->>B: IP Address
    B->>N: Sincronizar Horário
    N-->>B: Timestamp Atual
    B->>F: PUT /bikes/{bikeId}/scans/{timestamp}.json
    Note over B,F: Payload: bike, timestamp, networks[]
    F-->>B: 200 OK
    B->>B: Limpar Arquivos Locais
    B->>F: PUT /bikes/{bikeId}/status/{timestamp}.json
    Note over B,F: Status: conexões, bateria
    F-->>B: 200 OK
    B->>W: Desconectar
```

## 13. Monitoramento de Bateria

```mermaid
flowchart TD
    A[Leitura Bateria] --> B[analogRead A0]
    B --> C[Converter para Voltagem]
    C --> D[Calcular Percentual]
    D --> E{Percentual Válido?}
    E -->|Sim| F[Registrar Status]
    E -->|Não| G[Usar Valor Padrão]
    F --> H[Incluir em Dados]
    G --> H
    H --> I[Monitorar Carregamento]
    I --> J[Detectar Variações]
    J --> K[Log Eventos Bateria]
```

## Resumo dos Componentes

- **Configuração**: Arquivos TXT para parâmetros do sistema
- **Scanner WiFi**: Coleta periódica de redes disponíveis
- **Detecção de Base**: Proximidade por RSSI para 3 bases configuradas
- **Armazenamento Local**: JSON compacto em LittleFS
- **Upload Automático**: Sincronização com Firebase quando na base
- **Interface Web**: Configuração via AP ou base conectada
- **Menu Serial**: Controle e debug via porta serial
- **Status LED**: Indicação visual do estado do sistema
- **Monitoramento**: Bateria, conexões e status operacional