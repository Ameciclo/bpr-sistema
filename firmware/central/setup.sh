#!/bin/bash

echo "🚲 BPR Central Base Setup"
echo "========================="

# Check if firebase_config.json already exists
if [ -f "data/firebase_config.json" ]; then
    echo "⚠️  Configuração já existe!"
    read -p "Deseja sobrescrever? (y/N): " overwrite
    if [[ ! $overwrite =~ ^[Yy]$ ]]; then
        echo "Setup cancelado."
        exit 0
    fi
fi

# Get base configuration
echo
echo "📍 Configuração da Base:"
read -p "ID da base (ex: base01): " base_id
read -p "Nome da base (ex: Base Centro): " base_name
read -p "WiFi SSID: " wifi_ssid
read -s -p "WiFi Password: " wifi_password
echo

echo
echo "🔥 Configuração Firebase:"
read -p "Firebase Host (ex: projeto.firebaseio.com): " firebase_host
read -s -p "Firebase Auth Token: " firebase_auth
echo

# Create config file
cat > data/firebase_config.json << EOF
{
  "firebase_host": "$firebase_host",
  "firebase_auth": "$firebase_auth",
  "base_id": "$base_id",
  "base_name": "$base_name",
  "wifi_ssid": "$wifi_ssid",
  "wifi_password": "$wifi_password"
}
EOF

echo
echo "✅ Configuração criada em data/firebase_config.json"
echo
echo "🔧 Próximos passos:"
echo "1. pio run --target upload"
echo "2. pio device monitor"
echo
echo "🚲 Base $base_id configurada com sucesso!"