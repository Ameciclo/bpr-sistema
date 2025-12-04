#!/usr/bin/env node

const https = require('https');
const fs = require('fs');
const path = require('path');

// Configuração do Firebase
const FIREBASE_URL = 'https://botaprarodar-routes-default-rtdb.firebaseio.com';

function uploadToFirebase(path, data) {
    return new Promise((resolve, reject) => {
        const url = new URL(`${FIREBASE_URL}${path}.json`);
        
        const options = {
            hostname: url.hostname,
            port: 443,
            path: url.pathname,
            method: 'PUT',
            headers: {
                'Content-Type': 'application/json',
                'Content-Length': Buffer.byteLength(data)
            }
        };

        const req = https.request(options, (res) => {
            let responseData = '';
            
            res.on('data', (chunk) => {
                responseData += chunk;
            });
            
            res.on('end', () => {
                if (res.statusCode === 200) {
                    resolve(responseData);
                } else {
                    reject(new Error(`HTTP ${res.statusCode}: ${responseData}`));
                }
            });
        });

        req.on('error', (err) => {
            reject(err);
        });

        req.write(data);
        req.end();
    });
}

async function main() {
    try {
        console.log('🚀 Iniciando upload das configurações das centrais...');
        
        // Verificar se arquivo de config existe
        const configPath = path.join(__dirname, 'central_configs.json');
        if (!fs.existsSync(configPath)) {
            console.error('❌ Arquivo central_configs.json não encontrado!');
            console.log('💡 Execute: cp central_configs.json.example central_configs.json');
            console.log('💡 E edite as senhas WiFi antes de executar novamente.');
            process.exit(1);
        }
        
        // Carregar configurações
        const configs = JSON.parse(fs.readFileSync(configPath, 'utf8'));
        
        // Upload individual de cada central
        for (const [baseId, config] of Object.entries(configs)) {
            console.log(`📤 Enviando config para ${baseId}...`);
            
            const firebasePath = `/central_configs/${baseId}`;
            const configJson = JSON.stringify(config, null, 2);
            
            try {
                await uploadToFirebase(firebasePath, configJson);
                console.log(`✅ ${baseId} - Config enviada com sucesso`);
            } catch (error) {
                console.error(`❌ ${baseId} - Erro:`, error.message);
            }
        }
        
        console.log('ℹ️  As informações das bases estão nas configs das centrais');
        console.log('ℹ️  Para setup inicial, use o modo AP da central');
        
        console.log('🎉 Upload concluído!');
        
    } catch (error) {
        console.error('💥 Erro geral:', error.message);
        process.exit(1);
    }
}

main();