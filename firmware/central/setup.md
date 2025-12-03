# Setup da Central

## 1. Configuração Inicial

Copie o arquivo de exemplo e configure suas credenciais:

```bash
cp data/firebase_config_example.json data/firebase_config.json
```

## 2. Edite o arquivo `data/firebase_config.json`

```json
{
  "firebase_host": "seu-projeto.firebaseio.com",
  "firebase_auth": "sua_chave_auth_real",
  "base_id": "base01",
  "wifi_ssid": "Nome_WiFi_Real",
  "wifi_password": "senha_wifi_real"
}
```

## 3. Build e Upload

```bash
pio run --target upload
```

⚠️ **IMPORTANTE**: O arquivo `firebase_config.json` está no `.gitignore` e não deve ser commitado.