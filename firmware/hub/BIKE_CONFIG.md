# Sistema de Configuração de Bikes

## 🔄 Fluxo de Configuração

### 1. Solicitação de Configuração
Bike conecta via BLE e envia:
```json
{
  "type": "config_request", 
  "bike_id": "bpr-a1b2c3"
}
```

### 2. Verificação de Autorização
Hub verifica se bike está autorizada:
- ✅ **Whitelist**: Lista de bikes autorizadas em `/whitelist.json`
- ✅ **Auto-aprovação**: Bikes com prefixo `bpr-` ou `BPR-`
- ❌ **Negado**: Bikes não autorizadas

### 3. Resposta de Configuração
Hub responde com JSON completo:
```json
{
  "bike_id": "bpr-a1b2c3",
  "bike_name": "Bike Centro 01", 
  "version": 1,
  "dev_mode": false,
  "wifi": {
    "scan_interval_sec": 300, 
    "scan_timeout_ms": 5000
  },
  "ble": {
    "base_name": "BPR Hub Station", 
    "scan_time_sec": 5
  },
  "power": {
    "deep_sleep_duration_sec": 3600
  },
  "battery": {
    "critical_voltage": 3.2, 
    "low_voltage": 3.45
  }
}
```

### 4. Confirmação
Bike confirma recebimento:
```json
{
  "type": "config_received", 
  "bike_id": "bpr-a1b2c3",
  "status": "ok"
}
```

## 🔧 Implementação

### Características BLE
- **Serviço**: `12345678-1234-1234-1234-123456789abc`
- **Data**: `87654321-4321-4321-4321-cba987654321`
- **Config**: `11111111-2222-3333-4444-555555555555` ⭐ **NOVO**

### Arquivos Principais
- `bike_config.h/cpp` - Gerenciador de configurações
- `ble_only.cpp` - Característica BLE de configuração
- `whitelist.json` - Lista de bikes autorizadas

### Sistema de Aprovação
```json
{
  "auto_approve_bpr": true,
  "bikes": [
    "bpr-a1b2c3",
    "bpr-d4e5f6", 
    "bpr-g7h8i9"
  ]
}
```

## 🔥 Integração Firebase

### Estrutura de Dados
```
/bike_configs/{bike_id}     - Configurações específicas
/bike_config_logs/{hub_id}  - Logs de tentativas
/bike_whitelist/{hub_id}    - Lista de aprovação
```

### Logs Automáticos
Todas as tentativas são registradas:
- ✅ **Autorizadas**: Bike ID, timestamp, hub
- ❌ **Negadas**: Bike ID, timestamp, motivo

## 🧪 Testes

Execute o teste:
```cpp
// Compile test_bike_config.cpp
// Simula fluxo completo de configuração
```

## 📋 Configurações Disponíveis

| Campo | Tipo | Padrão | Descrição |
|-------|------|--------|-----------|
| `bike_id` | string | - | ID único da bike |
| `bike_name` | string | "Bike {id}" | Nome amigável |
| `version` | int | 1 | Versão da config |
| `dev_mode` | bool | false | Modo desenvolvimento |
| `wifi.scan_interval_sec` | int | 300 | Intervalo de scan WiFi |
| `wifi.scan_timeout_ms` | int | 5000 | Timeout do scan |
| `ble.base_name` | string | "BPR Hub Station" | Nome da base |
| `ble.scan_time_sec` | int | 5 | Tempo de scan BLE |
| `power.deep_sleep_duration_sec` | int | 3600 | Duração do sleep |
| `battery.critical_voltage` | float | 3.2 | Tensão crítica |
| `battery.low_voltage` | float | 3.45 | Tensão baixa |

## 🔄 Próximos Passos

1. **Firebase Integration**: Buscar configs específicas do Firebase
2. **Dynamic Updates**: Atualizar configs remotamente
3. **Validation**: Validar configs antes de enviar
4. **Versioning**: Sistema de versionamento de configs
5. **Rollback**: Reverter para config anterior em caso de erro