#!/bin/bash

# Cores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Função para log com timestamp
log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1"
}

# Função para carregar variáveis do .env
load_env() {
    if [ -f .env ]; then
        set -a
        source .env
        set +a
        log "Variáveis do .env carregadas"
    else
        echo -e "${RED}Erro: Arquivo .env não encontrado${NC}"
        exit 1
    fi
}

# Função para configurar Firebase Functions
setup_firebase_config() {
    log "Configurando variáveis do Firebase Functions..."
    
    firebase functions:config:set \
        telegram.bot_token="$TELEGRAM_BOT_TOKEN" \
        telegram.admin_chat_id="$ADMIN_CHAT_ID" \
        google.geolocation_api_key="$GOOGLE_GEOLOCATION_API_KEY"
    
    if [ $? -eq 0 ]; then
        log "${GREEN}Configurações do Firebase atualizadas com sucesso${NC}"
    else
        echo -e "${RED}Erro ao configurar Firebase Functions${NC}"
        exit 1
    fi
}

# Função para configurar webhook do Telegram
setup_webhook() {
    log "Configurando webhook do Telegram..."
    
    WEBHOOK_URL="https://us-central1-botaprarodar-routes.cloudfunctions.net/telegramWebhook"
    
    response=$(curl -s -X POST "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/setWebhook" \
        -d "url=$WEBHOOK_URL")
    
    if echo "$response" | grep -q '"ok":true'; then
        log "${GREEN}Webhook configurado com sucesso${NC}"
        log "URL: $WEBHOOK_URL"
    else
        echo -e "${RED}Erro ao configurar webhook:${NC}"
        echo "$response"
        exit 1
    fi
}

# Função para deploy completo
deploy_firebase() {
    log "Preparando para Firebase Functions..."
    load_env
    
    cd functions
    log "Instalando dependências..."
    npm install
    
    log "Fazendo build..."
    npm run build
    
    cd ..
    log "Configurando Firebase Functions..."
    setup_firebase_config
    
    log "Fazendo deploy para Firebase Functions..."
    firebase deploy --only functions
    
    if [ $? -eq 0 ]; then
        log "${GREEN}Deploy realizado com sucesso${NC}"
        setup_webhook
    else
        echo -e "${RED}Erro no deploy${NC}"
        exit 1
    fi
}

# Função para rodar localmente
run_local() {
    log "Iniciando bot localmente..."
    load_env
    
    cd functions
    
    if [ ! -d "node_modules" ]; then
        log "Instalando dependências..."
        npm install
    fi
    
    log "Fazendo build..."
    npm run build
    
    log "Iniciando bot local..."
    node lib/local-bot.js
}

# Função para ver logs
view_logs() {
    log "Visualizando logs do Firebase Functions..."
    firebase functions:log --only onNewSession,onNewScan,telegramWebhook
}

# Função para instalar dependências
install_deps() {
    log "Instalando dependências do projeto principal..."
    npm install
    
    log "Instalando dependências do Functions..."
    cd functions && npm install && cd ..
    
    log "${GREEN}Dependências instaladas com sucesso${NC}"
}

# Função para executar testes
run_tests() {
    log "Executando testes..."
    
    # Testar se .env existe
    if [ ! -f .env ]; then
        echo -e "${RED}❌ Arquivo .env não encontrado${NC}"
        return 1
    fi
    
    load_env
    
    # Testar se variáveis estão definidas
    if [ -z "$TELEGRAM_BOT_TOKEN" ]; then
        echo -e "${RED}❌ TELEGRAM_BOT_TOKEN não definido${NC}"
        return 1
    fi
    
    if [ -z "$ADMIN_CHAT_ID" ]; then
        echo -e "${RED}❌ ADMIN_CHAT_ID não definido${NC}"
        return 1
    fi
    
    # Testar conexão com Telegram API
    log "Testando conexão com Telegram API..."
    response=$(curl -s "https://api.telegram.org/bot$TELEGRAM_BOT_TOKEN/getMe")
    
    if echo "$response" | grep -q '"ok":true'; then
        echo -e "${GREEN}✅ Bot Token válido${NC}"
    else
        echo -e "${RED}❌ Bot Token inválido${NC}"
        return 1
    fi
    
    # Testar Firebase CLI
    log "Testando Firebase CLI..."
    if command -v firebase &> /dev/null; then
        echo -e "${GREEN}✅ Firebase CLI instalado${NC}"
    else
        echo -e "${RED}❌ Firebase CLI não encontrado${NC}"
        return 1
    fi
    
    log "${GREEN}Todos os testes passaram!${NC}"
}

# Menu principal
show_menu() {
    echo -e "${BLUE}🚴 Bot de Monitoramento de Bicicletas${NC}"
    echo "====================================="
    echo "1) Deploy para Firebase Functions"
    echo "2) Rodar localmente"
    echo "3) Ver logs do Functions"
    echo "4) Instalar dependências"
    echo "5) Executar testes"
    echo "6) Configurar apenas webhook"
    echo "7) Ver configurações do Firebase"
    echo "8) Sair"
    echo
}

# Loop principal
while true; do
    show_menu
    read -p "Escolha uma opção [1-8]: " choice
    
    case $choice in
        1)
            deploy_firebase
            ;;
        2)
            run_local
            ;;
        3)
            view_logs
            ;;
        4)
            install_deps
            ;;
        5)
            run_tests
            ;;
        6)
            load_env
            setup_webhook
            ;;
        7)
            firebase functions:config:get
            ;;
        8)
            log "Saindo..."
            exit 0
            ;;
        *)
            echo -e "${RED}Opção inválida${NC}"
            ;;
    esac
    
    echo
    read -p "Pressione Enter para continuar..."
    clear
done