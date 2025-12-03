#!/bin/bash

# WiFi Range Scanner - ESP32-C3
# Script de comandos principais

PORT="/dev/ttyACM0"  # ESP32-C3 geralmente usa ttyACM

show_menu() {
    echo "==================================="
    echo "  WiFi Range Scanner - ESP32-C3"
    echo "==================================="
    echo "1) Compilar e fazer upload"
    echo "2) Upload do sistema de arquivos (uploadfs)"
    echo "3) Monitor serial"
    echo "4) Verificar conexão com porta"
    echo "5) Configurar permissões USB"
    echo "6) Upload forçando porta específica"
    echo "7) Compilar apenas (sem upload)"
    echo "8) Limpar build"
    echo "9) Auto-detectar porta"
    echo "10) Selecionar configuração de bicicleta"
    echo "11) Baixar dados do dispositivo"
    echo "12) Upload com modo BOOT forçado"
    echo "13) Upload main_test.cpp (com BOOT forçado)"
    echo "0) Aguardar dispositivo"
    echo "q) Sair"
    echo "==================================="
    echo -n "Escolha uma opção: "
}

select_bike_config() {
    echo "=== CONFIGURAÇÕES DE BICICLETAS ==="
    echo "1) teste1 - Normal (60s)"
    echo "2) teste2 - Econômico (90s)"
    echo "3) teste3 - Intensivo (30s)"
    echo "4) teste4 - Mega Econômico (120s)"
    echo "5) teste5 - Extremo (15s)"
    echo "6) teste6 - Personalizado (45s/75s)"
    echo "0) Voltar"
    echo -n "Escolha a configuração: "
    
    read -r config_choice
    
    case $config_choice in
        1) copy_config "teste1" ;;
        2) copy_config "teste2" ;;
        3) copy_config "teste3" ;;
        4) copy_config "teste4" ;;
        5) copy_config "teste5" ;;
        6) copy_config "teste6" ;;
        0) return ;;
        *) echo "Opção inválida!" ;;
    esac
}

copy_config() {
    local bike_id=$1
    local config_file="configs/${bike_id}.txt"
    
    if [ -f "$config_file" ]; then
        echo "Copiando configuração $bike_id para data/config.txt..."
        cp "$config_file" "data/config.txt"
        echo "✓ Configuração $bike_id aplicada!"
        echo "Agora faça o upload do sistema de arquivos (opção 2)"
    else
        echo "✗ Arquivo de configuração $config_file não encontrado!"
    fi
}

download_device_data() {
    echo "=== BAIXAR DADOS DO DISPOSITIVO ==="
    echo "Conectando ao dispositivo para extrair dados..."
    
    # Re-verificar porta antes da conexão
    CURRENT_PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1)
    if [ -z "$CURRENT_PORT" ]; then
        echo "✗ Dispositivo não encontrado. Conecte o ESP32 via USB."
        return
    fi
    
    PORT="$CURRENT_PORT"
    echo "Dispositivo encontrado em: $PORT"
    
    local output_file="/home/daniel/device_data_$(date +%Y%m%d_%H%M%S).json"
    local temp_file="/tmp/esp_data_capture.txt"
    
    echo ""
    echo "=== CAPTURA AUTOMÁTICA DE DADOS ==="
    echo "1. O script vai abrir o monitor serial"
    echo "2. Digite 'm' para menu, depois '6' para transferir dados"
    echo "3. Os dados serão salvos automaticamente em: $output_file"
    echo "4. Use Ctrl+C para finalizar a captura"
    echo ""
    echo -n "Pressione ENTER para iniciar a captura: "
    read -r
    
    echo "Iniciando captura de dados..."
    echo "Arquivo será salvo em: $output_file"
    echo "Para menu: digite 'm', depois '6'"
    echo "Para sair: Ctrl+C"
    echo "========================="
    
    # Capturar dados do monitor serial
    pio device monitor --baud 115200 --port $PORT | tee "$temp_file"
    
    echo ""
    echo "✓ Monitor serial fechado."
    
    # Processar dados capturados
    if [ -f "$temp_file" ]; then
        echo "Processando dados capturados..."
        
        # Extrair dados entre === INICIO === e === FIM ===
        if grep -q "=== INICIO ===" "$temp_file" && grep -q "=== FIM ===" "$temp_file"; then
            sed -n '/=== INICIO ===/,/=== FIM ===/p' "$temp_file" | 
            sed '1d;$d' > "$output_file"
            
            if [ -s "$output_file" ]; then
                echo "✅ Dados salvos com sucesso em: $output_file"
                echo "📊 Tamanho do arquivo: $(wc -c < "$output_file") bytes"
                echo "📝 Linhas de dados: $(wc -l < "$output_file")"
            else
                echo "⚠ Arquivo criado mas está vazio"
            fi
        else
            echo "⚠ Marcadores de início/fim não encontrados"
            echo "Salvando captura completa em: $output_file"
            cp "$temp_file" "$output_file"
        fi
        
        # Limpar arquivo temporário
        rm -f "$temp_file"
    else
        echo "✗ Nenhum dado foi capturado"
    fi
}

check_port() {
    echo "Verificando portas USB disponíveis..."
    
    # Verificar portas USB seriais
    USB_PORTS=$(ls /dev/ttyUSB* 2>/dev/null)
    ACM_PORTS=$(ls /dev/ttyACM* 2>/dev/null)
    
    if [ -n "$USB_PORTS" ] || [ -n "$ACM_PORTS" ]; then
        echo "✓ Portas encontradas:"
        [ -n "$USB_PORTS" ] && echo "USB: $USB_PORTS"
        [ -n "$ACM_PORTS" ] && echo "ACM: $ACM_PORTS"
        
        echo "Porta configurada: $PORT"
        if [ -e "$PORT" ]; then
            echo "✓ Porta $PORT está disponível"
            # Verificar se é acessível
            if [ -r "$PORT" ] && [ -w "$PORT" ]; then
                echo "✓ Porta $PORT tem permissões corretas"
            else
                echo "⚠ Porta $PORT sem permissões (execute opção 5)"
            fi
        else
            echo "⚠ Porta $PORT não encontrada"
            FIRST_PORT=$(echo "$USB_PORTS $ACM_PORTS" | awk '{print $1}')
            [ -n "$FIRST_PORT" ] && echo "Sugestão: use $FIRST_PORT"
        fi
    else
        echo "✗ Nenhuma porta USB encontrada"
        echo "Verifique se o ESP8266 está conectado"
        echo "Dica: Desconecte e reconecte o cabo USB"
    fi
}

configure_permissions() {
    echo "Configurando permissões USB..."
    echo "Adicionando usuário ao grupo dialout..."
    sudo usermod -a -G dialout $USER
    echo "Aplicando mudanças de grupo..."
    newgrp dialout
    echo "✓ Configuração concluída"
    echo "⚠ Pode ser necessário fazer logout/login para aplicar as mudanças"
}

wait_for_device() {
    echo "Aguardando ESP8266..."
    echo "Pressione ENTER quando o dispositivo estiver conectado, ou 'q' para cancelar"
    
    while true; do
        # Verificar se há entrada do usuário
        if read -t 1 -n 1 input 2>/dev/null; then
            if [[ $input == "q" ]] || [[ $input == "Q" ]]; then
                echo "\\nCancelado pelo usuário"
                return 1
            fi
        fi
        
        # Verificar se dispositivo apareceu
        CURRENT_PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1)
        if [ -n "$CURRENT_PORT" ]; then
            PORT="$CURRENT_PORT"
            echo "\\n✓ ESP8266 detectado em: $PORT"
            return 0
        fi
        
        echo -n "."
    done
}

while true; do
    show_menu
    read -r choice
    
    case $choice in
        1)
            echo "=== COMPILAR E UPLOAD ==="
            echo "Compilando primeiro..."
            pio run
            if [ $? -eq 0 ]; then
                echo "✓ Compilação OK! Verificando porta para upload..."
                # Re-verificar porta antes do upload
                CURRENT_PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1)
                if [ -n "$CURRENT_PORT" ]; then
                    PORT="$CURRENT_PORT"
                    echo "Porta detectada: $PORT"
                    echo "Iniciando upload..."
                    pio run --target upload --upload-port $PORT
                    echo "Upload concluído!"
                else
                    echo "✗ ESP8266 não encontrado."
                    echo "Reconecte o cabo USB e pressione ENTER, ou 'q' para cancelar"
                    if wait_for_device; then
                        echo "Tentando upload novamente..."
                        pio run --target upload --upload-port $PORT
                        echo "Upload concluído!"
                    fi
                fi
            else
                echo "✗ Erro na compilação"
            fi
            ;;
        2)
            echo "=== UPLOAD SISTEMA DE ARQUIVOS ==="
            echo "⚠ ATENÇÃO: Isso apagará todos os dados coletados!"
            echo -n "Continuar? (s/N): "
            read -r confirm
            if [[ $confirm =~ ^[Ss]$ ]]; then
                # Re-verificar porta antes do upload
                CURRENT_PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1)
                if [ -n "$CURRENT_PORT" ]; then
                    PORT="$CURRENT_PORT"
                    echo "Porta detectada: $PORT"
                    echo "Fazendo upload do sistema de arquivos..."
                    pio run --target uploadfs --upload-port $PORT
                    echo "Upload FS concluído!"
                else
                    echo "✗ ESP8266 não encontrado. Reconecte o cabo USB."
                fi
            else
                echo "Upload cancelado"
            fi
            ;;
        3)
            echo "=== MONITOR SERIAL ==="
            # Re-verificar porta antes do monitor
            CURRENT_PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1)
            if [ -n "$CURRENT_PORT" ]; then
                PORT="$CURRENT_PORT"
                echo "Conectando ao monitor serial em $PORT..."
                echo "Para sair: Ctrl+C"
                echo "Para menu da bike: digite 'm'"
                echo "=========================="
                pio device monitor --baud 115200 --port $PORT
            else
                echo "✗ ESP8266 não encontrado. Reconecte o cabo USB."
            fi
            ;;
        4)
            echo "=== VERIFICAR CONEXÃO ==="
            check_port
            ;;
        5)
            echo "=== CONFIGURAR PERMISSÕES ==="
            configure_permissions
            ;;
        6)
            echo "=== UPLOAD COM PORTA ESPECÍFICA ==="
            echo -n "Digite a porta (ex: /dev/ttyUSB0): "
            read -r custom_port
            if [ -e "$custom_port" ]; then
                echo "Fazendo upload para $custom_port..."
                pio run --target upload --upload-port $custom_port
            else
                echo "✗ Porta $custom_port não encontrada"
            fi
            ;;
        7)
            echo "=== COMPILAR APENAS ==="
            echo "Compilando projeto..."
            pio run
            echo "Compilação concluída!"
            ;;
        8)
            echo "=== LIMPAR BUILD ==="
            echo "Limpando arquivos de build..."
            pio run --target clean
            echo "Build limpo!"
            ;;
        9)
            echo "=== AUTO-DETECTAR PORTA ==="
            DETECTED_PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1)
            if [ -n "$DETECTED_PORT" ]; then
                PORT="$DETECTED_PORT"
                echo "✓ Porta detectada e configurada: $PORT"
            else
                echo "✗ Nenhuma porta encontrada"
            fi
            ;;
        10)
            echo "=== SELECIONAR CONFIGURAÇÃO ==="
            select_bike_config
            ;;
        11)
            echo "=== BAIXAR DADOS DO DISPOSITIVO ==="
            download_device_data
            ;;
        12)
            echo "=== UPLOAD COM MODO BOOT FORÇADO ==="
            echo "⚠ INSTRUÇÕES:"
            echo "1. Pressione e SEGURE o botão BOOT no ESP32"
            echo "2. Pressione ENTER para iniciar o upload"
            echo "3. CONTINUE segurando BOOT até ver 'Connecting...'"
            echo "4. Solte o botão BOOT quando começar o upload"
            echo ""
            echo -n "Pressione ENTER quando estiver segurando o botão BOOT: "
            read -r
            
            # Re-verificar porta
            CURRENT_PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1)
            if [ -n "$CURRENT_PORT" ]; then
                PORT="$CURRENT_PORT"
                echo "Porta detectada: $PORT"
                echo "Iniciando upload com modo BOOT forçado..."
                echo "CONTINUE segurando BOOT até ver 'Connecting...'"
                
                # Upload com flags mais agressivas
                pio run --target upload --upload-port $PORT
                
                echo ""
                echo "✅ Upload concluído! Agora pode soltar o botão BOOT."
            else
                echo "❌ ESP32 não encontrado. Reconecte o cabo USB."
            fi
            ;;
        13)
            echo "=== UPLOAD MAIN_TEST.CPP (COM BOOT FORÇADO) ==="
            echo "⚠ Isso vai compilar e subir o main_test.cpp"
            echo "⚠ INSTRUÇÕES:"
            echo "1. Pressione e SEGURE o botão BOOT no ESP32"
            echo "2. Pressione ENTER para iniciar"
            echo "3. CONTINUE segurando BOOT até ver 'Connecting...'"
            echo "4. Solte o botão BOOT quando começar o upload"
            echo ""
            echo -n "Pressione ENTER quando estiver segurando o botão BOOT: "
            read -r
            
            # Mover TODOS os arquivos .cpp exceto main_test.cpp para evitar conflitos
            echo "Criando backup temporário de todos os arquivos .cpp..."
            mkdir -p temp_backup
            
            # Mover todos os .cpp exceto main_test.cpp
            for file in src/*.cpp; do
                if [ "$file" != "src/main_test.cpp" ]; then
                    mv "$file" "temp_backup/"
                fi
            done
            
            # Mover todos os .h para backup também
            for file in src/*.h; do
                if [ -f "$file" ]; then
                    mv "$file" "temp_backup/"
                fi
            done
            
            # Renomear main_test.cpp para main.cpp
            if [ -f "src/main_test.cpp" ]; then
                echo "Renomeando main_test.cpp para main.cpp..."
                mv "src/main_test.cpp" "src/main.cpp"
                
                # Re-verificar porta
                CURRENT_PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1)
                if [ -n "$CURRENT_PORT" ]; then
                    PORT="$CURRENT_PORT"
                    echo "Porta detectada: $PORT"
                    echo "Compilando e fazendo upload do main_test.cpp..."
                    echo "CONTINUE segurando BOOT até ver 'Connecting...'"
                    
                    # Compilar e fazer upload
                    pio run --target upload --upload-port $PORT
                    
                    echo ""
                    echo "✅ Upload do main_test.cpp concluído!"
                    
                    # Restaurar todos os arquivos originais
                    echo "Restaurando todos os arquivos originais..."
                    mv "src/main.cpp" "src/main_test.cpp"
                    
                    if [ -d "temp_backup" ]; then
                        mv temp_backup/* src/
                        rmdir temp_backup
                        echo "✅ Todos os arquivos restaurados!"
                    fi
                else
                    echo "❌ ESP32 não encontrado. Reconecte o cabo USB."
                    # Restaurar arquivos mesmo em caso de erro
                    if [ -d "temp_backup" ]; then
                        mv "src/main.cpp" "src/main_test.cpp" 2>/dev/null
                        mv temp_backup/* src/ 2>/dev/null
                        rmdir temp_backup 2>/dev/null
                        echo "Arquivos restaurados após erro"
                    fi
                fi
            else
                echo "❌ main_test.cpp não encontrado!"
            fi
            ;;
        0)
            echo "=== AGUARDAR DISPOSITIVO ==="
            wait_for_device
            ;;
        q|Q)
            echo "Saindo..."
            exit 0
            ;;
        *)
            echo "Opção inválida!"
            ;;
    esac
    
    echo ""
    echo "Pressione Enter para continuar..."
    read -r
    clear
done