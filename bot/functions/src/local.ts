import * as dotenv from 'dotenv';
import * as path from 'path';

// Carrega variáveis do .env na raiz do projeto
dotenv.config({ path: path.join(__dirname, '../../.env') });

// Importa o bot após carregar as variáveis
import './index';

console.log('🚀 Bot rodando localmente...');