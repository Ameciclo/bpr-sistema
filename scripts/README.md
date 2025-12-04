# Scripts de Configuração

## Setup das Configurações das Centrais

### 1. Copiar arquivo de exemplo
```bash
cp central_configs.json.example central_configs.json
```

### 2. Editar senhas WiFi
Abra `central_configs.json` e substitua `YOUR_WIFI_PASSWORD_HERE` pelas senhas reais:

```json
{
  "ameciclo": {
    "wifi": {
      "ssid": "WIFI_AMECICLO",
      "password": "senha_real_aqui"
    }
  }
}
```

### 3. Upload para Firebase
```bash
node upload_central_configs.js
```

## ⚠️ Importante
- O arquivo `central_configs.json` está no `.gitignore` e não deve ser commitado
- Apenas o arquivo `.example` deve ir para o repositório
- Mantenha as senhas seguras e não as compartilhe