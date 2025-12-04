# 🔧 Setup Inicial da Central - Modo AP

## 🚨 Problema: Configuração Circular
- Central precisa de WiFi para baixar config
- Mas a config tem as credenciais do WiFi!

## 💡 Solução: Modo AP de Configuração

### **Como Funciona:**

1. **Primeira vez** (sem config):
   - Central entra em **Modo AP** automático
   - Cria rede: `BPR_Setup_XXXXXX` (XXXXXX = MAC)
   - Senha: `bpr12345`
   - LED: Piscar alternado (azul/off a cada 1s)

2. **Interface Web** (192.168.4.1):
   ```
   📱 BPR Central - Configuração Inicial
   
   Base ID: [ameciclo/cepas/ctresiste] ▼
   
   WiFi:
   SSID: [_____________]
   Senha: [_____________]
   
   Firebase URL: [https://bpr-sistema-default-rtdb.firebaseio.com]
   
   [🔄 Testar Conexão] [💾 Salvar e Reiniciar]
   ```

3. **Processo**:
   - Usuário seleciona base_id
   - Sistema baixa config completa do Firebase
   - Salva localmente
   - Reinicia em modo normal

### **Implementação:**

```cpp
// Detectar se precisa de setup inicial
bool needsInitialSetup() {
    return !LittleFS.exists("/config.json") || 
           !LittleFS.exists("/central_config.json");
}

// Modo AP de configuração
void startConfigAP() {
    WiFi.mode(WIFI_AP);
    String apName = "BPR_Setup_" + WiFi.macAddress().substring(9);
    WiFi.softAP(apName.c_str(), "bpr12345");
    
    // Web server para configuração
    setupConfigWebServer();
    
    // LED especial para modo setup
    setLEDPattern(LED_SETUP_MODE);
}
```

### **Vantagens:**
- ✅ Setup inicial simples
- ✅ Não precisa hardcoded WiFi
- ✅ Pode reconfigurar remotamente depois
- ✅ Fallback se perder config

### **Fluxo Completo:**
1. **Flash firmware** → Modo AP automático
2. **Conecta no AP** → Configura base_id + WiFi
3. **Baixa config** → Salva local + reinicia
4. **Modo normal** → BLE + sync periódico
5. **Reconfig remota** → Via Firebase (sem AP)

## 🔄 Reconfiguração Posterior

Depois do setup inicial, mudanças de WiFi/Firebase podem ser feitas:

1. **Via Firebase** - Muda config remota
2. **Via botão físico** - Segura 10s → volta ao modo AP
3. **Via comando BLE** - Bike pode forçar reconfig

Assim resolve o problema circular e fica fácil de configurar! 🚲⚙️