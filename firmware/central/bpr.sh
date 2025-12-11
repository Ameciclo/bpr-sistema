#!/bin/bash
# 🚲 BPR Central - Script Unificado
# Todas as operações em um só lugar

set -e

show_menu() {
    clear
    echo "🚲 BPR CENTRAL - MENU PRINCIPAL"
    echo "==============================="
    echo
    echo "📋 CONFIGURAÇÃO:"
    echo "  1) 🆕 Setup Nova Central (config + upload)"
    echo "  2) ⚙️  Configurar Apenas (sem upload)"
    echo "  3) 🔧 Corrigir Config Existente"
    echo
    echo "📤 UPLOAD:"
    echo "  4) ⬆️  Upload Completo (config + firmware)"
    echo "  5) 📁 Upload Apenas Config"
    echo "  6) 💾 Upload Apenas Firmware"
    echo "  7) 🌐 Upload Firmware Limpo (AP mode)"
    echo
    echo "🛠️  MANUTENÇÃO:"
    echo "  8) 📊 Monitor Serial"
    echo "  9) 🧹 Limpar Build"
    echo " 10) 🗂️  Limpar Filesystem ESP32"
    echo " 11) 🔥 Apagar ESP32 Completo"
    echo " 12) 🗑️  Limpar Arquivos Não Usados"
    echo
    echo " 0) ❌ Sair"
    echo
}

check_pio() {
    if ! command -v pio &> /dev/null; then
        echo "❌ PlatformIO não encontrado!"
        echo "💡 Instale: pip install platformio"
        exit 1
    fi
}

setup_new_central() {
    echo "🆕 SETUP NOVA CENTRAL"
    echo "===================="
    echo
    read -p "🆔 Base ID (ex: ameciclo): " base_id
    read -p "📍 Nome (ex: Ameciclo): " base_name
    read -p "📶 WiFi SSID: " wifi_ssid
    read -s -p "🔑 WiFi Password: " wifi_password
    echo
    
    if [[ -z "$base_id" || -z "$wifi_ssid" || -z "$wifi_password" ]]; then
        echo "❌ Campos obrigatórios!"
        return 1
    fi
    
    mkdir -p data
    cat > data/config.json << EOF
{
  "base_id": "$base_id",
  "wifi": {
    "ssid": "$wifi_ssid",
    "password": "$wifi_password"
  },
}
EOF
    
    echo "⚠️  IMPORTANTE: Certifique-se que existe /central_configs/$base_id no Firebase!"
    echo "📋 Estrutura necessária:"
    echo "   - base_id: $base_id"
    echo "   - last_modified: timestamp"
    echo "   - sync_interval_sec, led_pin, etc."
    echo "   - central: {name, max_bikes, location}"
    echo "   - wifi: {ssid, password}"
    echo "   - led: {boot_ms, ble_ready_ms, etc.}"
    
    echo "✅ Config criada!"
    echo
    echo "⚠️  IMPORTANTE: Certifique-se que existe /central_configs/$base_id no Firebase!"
    echo "📋 Estrutura necessária:"
    echo "   - base_id: $base_id"
    echo "   - last_modified: timestamp"
    echo "   - sync_interval_sec, led_pin, etc."
    echo "   - central: {name, max_bikes, location}"
    echo "   - wifi: {ssid, password}"
    echo "   - led: {boot_ms, ble_ready_ms, etc.}"
    echo
    read -p "📤 Fazer upload agora? (y/N): " upload_now
    if [[ $upload_now =~ ^[Yy]$ ]]; then
        upload_complete
    fi
}

config_only() {
    echo "⚙️ CONFIGURAR APENAS"
    echo "=================="
    setup_new_central
}

fix_config() {
    echo "🔧 CORRIGIR CONFIG"
    echo "================="
    
    if [[ ! -f "data/config.json" ]]; then
        echo "❌ Config não existe! Use opção 1 primeiro."
        return 1
    fi
    
    echo "📄 Config atual:"
    cat data/config.json
    echo
    read -p "🔄 Recriar config? (y/N): " recreate
    if [[ $recreate =~ ^[Yy]$ ]]; then
        setup_new_central
    fi
}

upload_complete() {
    echo "⬆️ UPLOAD COMPLETO"
    echo "================="
    check_pio
    
    echo "🔨 Compilando..."
    pio run
    
    echo "📁 Upload config..."
    pio run --target uploadfs
    
    echo "💾 Upload firmware..."
    pio run --target upload
    
    echo "✅ Upload completo!"
}

upload_config() {
    echo "📁 UPLOAD CONFIG"
    echo "==============="
    check_pio
    pio run --target uploadfs
    echo "✅ Config enviada!"
}

upload_firmware() {
    echo "💾 UPLOAD FIRMWARE"
    echo "================="
    check_pio
    pio run --target upload
    echo "✅ Firmware enviado!"
}

upload_blank() {
    echo "🌐 UPLOAD LIMPO (AP MODE)"
    echo "========================"
    check_pio
    
    rm -rf data/
    echo "🔨 Compilando..."
    pio run
    echo "⬆️ Upload..."
    pio run --target upload
    
    echo "✅ ESP32 vai criar AP: BPR_Setup"
    echo "📱 Conecte e acesse: 192.168.4.1"
}

monitor_serial() {
    echo "📊 MONITOR SERIAL"
    echo "================"
    check_pio
    pio device monitor
}

clean_build() {
    echo "🧹 LIMPANDO BUILD"
    echo "================="
    check_pio
    pio run --target clean
    echo "✅ Build limpo!"
}

clear_filesystem() {
    echo "🗂️ LIMPAR FILESYSTEM ESP32"
    echo "======================="
    echo "⚠️ Isso remove todos os arquivos salvos no ESP32:"
    echo "   • config_cache.json"
    echo "   • ble_config.json"
    echo "   • Outros arquivos de cache"
    echo
    read -p "🗑️ Confirma limpeza do filesystem? (y/N): " confirm
    if [[ $confirm =~ ^[Yy]$ ]]; then
        check_pio
        
        # Criar sketch temporário para limpar FS
        cat > temp_clear_fs.cpp << 'EOF'
#include <Arduino.h>
#include <LittleFS.h>

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("🧹 Limpando filesystem...");
    
    if (!LittleFS.begin()) {
        Serial.println("❌ Falha ao montar LittleFS");
        return;
    }
    
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("❌ Falha ao abrir diretório raiz");
        return;
    }
    
    File file = root.openNextFile();
    int removed = 0;
    
    while (file) {
        String fileName = file.name();
        file.close();
        
        if (LittleFS.remove("/" + fileName)) {
            Serial.printf("🗑️ Removido: %s\n", fileName.c_str());
            removed++;
        }
        
        file = root.openNextFile();
    }
    
    root.close();
    Serial.printf("✅ Filesystem limpo - %d arquivos removidos\n", removed);
    Serial.println("🔄 Reinicie o ESP32 para aplicar");
}

void loop() {
    delay(1000);
}
EOF
        
        # Backup main.cpp
        cp src/main.cpp src/main.cpp.bak
        
        # Usar sketch temporário
        cp temp_clear_fs.cpp src/main.cpp
        
        echo "📎 Compilando limpador..."
        pio run --target upload
        
        # Restaurar main.cpp
        mv src/main.cpp.bak src/main.cpp
        rm temp_clear_fs.cpp
        
        echo "✅ Filesystem limpo!"
        echo "📝 Agora faça upload do firmware normal"
    fi
}

erase_esp32() {
    echo "🔥 APAGAR ESP32"
    echo "=============="
    echo "⚠️ Isso apaga TUDO da memória!"
    read -p "🗑️ Confirma? (y/N): " confirm
    if [[ $confirm =~ ^[Yy]$ ]]; then
        check_pio
        pio run --target erase
        echo "✅ ESP32 apagado!"
    fi
}

cleanup_unused() {
    echo "🗑️ LIMPAR ARQUIVOS NÃO USADOS"
    echo "============================"
    
    files_to_remove=(
        "src/ble_central.cpp" "src/ble_central.h"
        "src/ble_working.cpp" "src/ble_working.h"
        "src/buffer_manager.cpp" "src/buffer_manager.h"
        "src/config_loader.cpp" "src/config_loader.h"
        "src/event_handler.cpp" "src/event_handler.h"
        "src/firebase_client.h" "src/firebase_sync.h"
        "src/self_check.cpp" "src/self_check.h"
        "src/wifi_manager.cpp" "src/wifi_manager.h"
        "setup.sh" "upload.sh" "erase.sh" "fix_config.sh"
        "test_ble.sh" "upload_blank.sh" "cleanup_unused.sh"
    )
    
    for file in "${files_to_remove[@]}"; do
        if [[ -f "$file" ]]; then
            rm "$file"
            echo "🗑️ Removido: $file"
        fi
    done
    
    echo "✅ Limpeza concluída!"
}

# Menu principal
while true; do
    show_menu
    read -p "Escolha uma opção: " choice
    echo
    
    case $choice in
        1) setup_new_central ;;
        2) config_only ;;
        3) fix_config ;;
        4) upload_complete ;;
        5) upload_config ;;
        6) upload_firmware ;;
        7) upload_blank ;;
        8) monitor_serial ;;
        9) clean_build ;;
        10) clear_filesystem ;;
        11) erase_esp32 ;;
        12) cleanup_unused ;;
        0) echo "👋 Tchau!"; exit 0 ;;
        *) echo "❌ Opção inválida!" ;;
    esac
    
    echo
    read -p "Pressione Enter para continuar..."
done