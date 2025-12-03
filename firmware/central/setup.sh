#!/bin/bash

echo "🚲 BPR Central Base Setup"
echo "========================="

# Create data directory if it doesn't exist
mkdir -p data

# Check if config.json already exists
if [ -f "data/config.json" ]; then
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
read -p "Firebase API Key: " firebase_api_key
read -p "Database URL (ex: https://projeto-default-rtdb.firebaseio.com/): " database_url
echo

# Create config file
cat > data/config.json << EOF
{
  "wifi": {
    "ssid": "$wifi_ssid",
    "password": "$wifi_password"
  },
  "firebase": {
    "api_key": "$firebase_api_key",
    "database_url": "$database_url"
  },
  "central": {
    "id": "$base_id",
    "name": "$base_name",
    "max_bikes": 10,
    "location": {
      "lat": -8.062,
      "lng": -34.881
    }
  }
}
EOF

echo
echo "✅ Configuração criada em data/config.json"
echo
echo "🔧 Próximos passos:"
echo "1. pio run --target uploadfs  # Upload config"
echo "2. pio run --target upload     # Upload firmware"
echo "3. pio device monitor          # Monitor serial"
echo
echo "🚲 Base $base_id configurada com sucesso!"