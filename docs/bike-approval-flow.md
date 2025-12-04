# 🚲 Fluxo de Aprovação de Bikes Novas

## 📋 Processo Completo

### 1. Descoberta Automática
```
Bike nova → Liga pela primeira vez
         → Nome BLE: "BPR_A1B2C3" (últimos 6 do MAC)
         → Escaneia por centrais: "BPR_BASE_*"
         → Conecta na central encontrada
         → Se identifica como "BPR_A1B2C3"
```

### 2. Registro Pendente
```
Central → Detecta bike nova
        → Registra no Firebase: /pending_bikes/ameciclo/BPR_A1B2C3
        → Status: "pending"
        → Aguarda aprovação humana
```

### 3. Aprovação Humana

#### Via Dashboard Web:
```
🚲 Nova Bike Detectada!
📍 Central: Ameciclo  
🔗 BLE: BPR_A1B2C3
📱 MAC: AA:BB:CC:A1:B2:C3
⏰ Detectada: 14:30

[✅ Aprovar] [❌ Rejeitar]
```

#### Via Bot Telegram:
```
🚲 *Nova bike detectada!*
📍 Base: Ameciclo
🆔 ID: BPR_A1B2C3
📱 MAC: AA:BB:CC:A1:B2C3
⏰ Há 5 minutos

/aprovar_BPR_A1B2C3
/rejeitar_BPR_A1B2C3
```

### 4. Configuração Automática
```
Aprovação → Firebase: status = "approved" + bike_id = "bikeA1B2C3"
Central → Detecta aprovação na próxima sync
        → Prepara configuração completa
        → Aguarda bike conectar novamente
        → Envia config via BLE
        → Bike salva e reinicia como "bikeA1B2C3"
```

## 🔥 Estrutura Firebase

### `/pending_bikes/{central_id}/{ble_name}`
```json
{
  "BPR_A1B2C3": {
    "mac_address": "AA:BB:CC:A1:B2:C3",
    "first_seen": 1764802387,
    "central_id": "ameciclo",
    "status": "pending",
    "approved_by": null,
    "approved_at": null
  }
}
```

### Após Aprovação:
```json
{
  "BPR_A1B2C3": {
    "mac_address": "AA:BB:CC:A1:B2:C3", 
    "first_seen": 1764802387,
    "central_id": "ameciclo",
    "status": "approved",
    "approved_by": "admin_user",
    "approved_at": 1764802500,
    "bike_id": "bikeA1B2C3"
  }
}
```

## 🛡️ Segurança

### ✅ Vantagens:
- **Controle Total**: Só bikes aprovadas podem se configurar
- **Auditoria**: Registro de quem aprovou e quando
- **Prevenção**: Evita bikes não autorizadas na rede
- **Flexibilidade**: Aprovação remota via web/bot

### 🔐 Proteções:
- Prefixo BLE obrigatório: `BPR_*`
- Aprovação humana obrigatória
- Registro de MAC address para rastreamento
- Timeout automático para pendências antigas

## 🚀 Implementação

### Central (ESP32):
1. Anuncia como `BPR_BASE_{central_id}`
2. Detecta bikes com prefixo `BPR_*`
3. Registra no Firebase como pendente
4. Verifica aprovações a cada sync
5. Configura bikes aprovadas

### Dashboard/Bot:
1. Monitora `/pending_bikes/`
2. Notifica administradores
3. Interface de aprovação/rejeição
4. Atualiza status no Firebase

### Bike (ESP32):
1. Primeira vez: anuncia como `BPR_{MAC}`
2. Escaneia por centrais `BPR_BASE_*`
3. Conecta e aguarda configuração
4. Após config: funciona como `bike{ID}`

## 📊 Monitoramento

### Métricas Importantes:
- Bikes pendentes por central
- Tempo médio de aprovação
- Bikes rejeitadas (possível invasão)
- Taxa de sucesso de configuração

### Alertas:
- Muitas bikes pendentes (> 5)
- Bikes não aprovadas há muito tempo (> 24h)
- Tentativas de conexão suspeitas
- Falhas de configuração