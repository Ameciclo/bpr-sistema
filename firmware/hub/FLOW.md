## 1️⃣ Diagrama geral da máquina de estados do HUB

```mermaid
stateDiagram-v2
    [*] --> BOOT

    BOOT --> CONFIG_AP : config inexistente\nou inválida
    BOOT --> WIFI_SYNC : config válida\n(firstSync=true)

    CONFIG_AP --> CONFIG_AP : usuário configurando
    CONFIG_AP --> BOOT : timeout (restart)
    CONFIG_AP --> BOOT : config salva (restart)

    WIFI_SYNC --> BLE_ONLY : sync OK
    WIFI_SYNC --> CONFIG_AP : firstSync falha

    BLE_ONLY --> WIFI_SYNC : EVENT_SYNC_TRIGGER\n(buffer cheio ou timer)
    BLE_ONLY --> SHUTDOWN : inatividade\n+ 0 bikes

    SHUTDOWN --> BLE_ONLY : wake-up timer\nou evento BLE

    BLE_ONLY --> CONFIG_AP : fallback\n(muitas falhas de sync)
```

---

## 2️⃣ Fluxo detalhado de inicialização (BOOT → CONFIG_AP ou WIFI_SYNC)

```mermaid
flowchart TD
    A[Power ON / Reset] --> B[setup()]
    B --> C[LittleFS.begin]
    C -->|Falha| R[Restart]

    C --> D[configManager.loadConfig]
    D --> E[bufferManager.begin]
    E --> F[ledController.begin + bootPattern]

    D -->|Config inválida| G[STATE_CONFIG_AP]
    D -->|Config válida| H[STATE_WIFI_SYNC]

    G --> G1[WiFi AP ON\n192.168.4.1]
    G1 --> G2[Servidor Web ativo]
    G2 -->|Timeout 15min| R
    G2 -->|Salvar config| R

    H --> H1[firstSync = true]
```

---

## 3️⃣ Fluxo operacional principal (BLE_ONLY)

Esse é o **estado dominante** do hub.

```mermaid
flowchart TD
    A[STATE_BLE_ONLY] --> B[BLE Server ativo]
    B --> C[Advertising BLE]
    C --> D{Bike conecta?}

    D -->|Sim| E[onConnect]
    E --> E1[connectedBikes++]
    E1 --> E2[LED bikeArrived]
    E2 --> C

    D -->|Não| F[BLEOnly::update]

    F --> G{Tempo > sync_interval?}
    G -->|Sim| H[EVENT_SYNC_TRIGGER]
    H --> I[STATE_WIFI_SYNC]

    G -->|Não| J{Heartbeat interval?}
    J -->|Sim| K[buffer.addHeartbeat]

    F --> L{Inatividade + 0 bikes?}
    L -->|Sim| M[STATE_SHUTDOWN]
```

---

## 4️⃣ Comunicação BLE com bicicletas (Config + Dados)

```mermaid
sequenceDiagram
    participant Bike
    participant HubBLE as BLE Server
    participant Buffer
    participant ConfigMgr

    Bike ->> HubBLE: Connect
    HubBLE ->> Bike: Advertise services

    Bike ->> HubBLE: Write DATA characteristic
    HubBLE ->> Buffer: addData()

    Bike ->> HubBLE: Write CONFIG request
    HubBLE ->> ConfigMgr: isBikeAuthorized?
    ConfigMgr ->> HubBLE: Config JSON
    HubBLE ->> Bike: Notify CONFIG

    Bike ->> HubBLE: config_received ACK
```

---

## 5️⃣ Fluxo completo de sincronização Wi-Fi (WIFI_SYNC)

Este é o trecho mais crítico e está **muito bem definido no seu código**.

```mermaid
flowchart TD
    A[STATE_WIFI_SYNC] --> B[WiFi.begin]
    B -->|Falha| X[Sync fail]

    B -->|Conectado| C[NTP sync]
    C --> D[downloadConfig]

    D -->|Config válida| E[configManager.update]
    D -->|Inválida| F[Usar config local]

    E --> G[uploadData]
    G --> H[uploadHeartbeat]

    H -->|Sucesso| I[recordSyncSuccess]
    I --> J[STATE_BLE_ONLY]

    H -->|Falha| K[recordSyncFailure]
    K --> L{firstSync?}
    L -->|Sim| M[STATE_CONFIG_AP]
    L -->|Não| J
```

---

## 6️⃣ Buffer e persistência offline (essencial)

```mermaid
flowchart TD
    A[BLE Data Recebido] --> B[buffer.addData]
    B --> C{dataCount >= 40?}
    C -->|Sim| D[needsSync = true]

    B --> E{a cada 10 itens}
    E -->|Sim| F[saveState LittleFS]

    D --> G[WIFI_SYNC]
    G --> H{Upload OK?}
    H -->|Sim| I[markAsSent]
    H -->|Não| J[Manter buffer]
```

---

## 7️⃣ Shutdown / economia de energia

```mermaid
flowchart TD
    A[STATE_SHUTDOWN] --> B[save buffer + config]
    B --> C[LED OFF]
    C --> D[light sleep 30min]

    D -->|Wake timer| E[EVENT_WAKE_UP]
    E --> F[STATE_BLE_ONLY]
```

---

## 8️⃣ Visão sistêmica final (tudo junto)

```mermaid
graph TD
    Hub -->|BLE| Bikes
    Bikes -->|Data + Requests| Hub

    Hub -->|Buffer| LittleFS
    Hub -->|WiFi| Firebase

    Firebase -->|Config| Hub
    Hub -->|Logs + Heartbeat| Firebase

    Hub -->|AP Mode| User
```

---

## Conclusão

Este diagrama:

* ✅ Representa **exatamente** o código atual
* ✅ Explica por que BLE e Wi-Fi não rodam juntos
* ✅ Mostra claramente os critérios de transição
* ✅ Serve como documentação técnica real
* ✅ Pode ser usado para:
  * README
  * Paper técnico
  * Treinar outra IA
  * Onboarding de novos devs