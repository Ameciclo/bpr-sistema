import * as dotenv from 'dotenv';
import * as path from 'path';

// Carrega variáveis do .env na raiz do projeto
dotenv.config({ path: path.join(__dirname, '../../.env') });

// Define NODE_ENV como development para usar as variáveis locais
process.env.NODE_ENV = 'development';

// Importa o bot após carregar as variáveis
import { Telegraf } from 'telegraf';
import * as admin from 'firebase-admin';

// Inicializa Firebase com credenciais locais
admin.initializeApp({
  credential: admin.credential.cert({
    projectId: process.env.FIREBASE_PROJECT_ID,
    clientEmail: process.env.FIREBASE_CLIENT_EMAIL,
    privateKey: process.env.FIREBASE_PRIVATE_KEY?.replace(/\\n/g, '\n')
  }),
  databaseURL: process.env.FIREBASE_DATABASE_URL
});

const db = admin.database();
const bot = new Telegraf(process.env.TELEGRAM_BOT_TOKEN!);

// Função para calcular distância entre coordenadas GPS
function calculateGPSDistance(lat1: number, lng1: number, lat2: number, lng2: number): number {
  const R = 6371000; // Raio da Terra em metros
  const dLat = (lat2 - lat1) * Math.PI / 180;
  const dLng = (lng2 - lng1) * Math.PI / 180;
  const a = Math.sin(dLat/2) * Math.sin(dLat/2) +
            Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
            Math.sin(dLng/2) * Math.sin(dLng/2);
  const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
  return R * c;
}

// Função para converter redes WiFi em coordenadas
async function convertWifiToCoordinates(networks: any[]) {
  try {
    // Formato: [ssid, bssid, rssi, channel]
    const wifiAccessPoints = networks.map(net => ({
      macAddress: net[1], // BSSID real
      signalStrength: net[2], // RSSI
      channel: net[3] // Channel
    }));

    const response = await fetch('https://www.googleapis.com/geolocation/v1/geolocate?key=' + process.env.GOOGLE_GEOLOCATION_API_KEY, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ wifiAccessPoints })
    });

    if (response.ok) {
      const data = await response.json();
      return {
        lat: data.location.lat,
        lng: data.location.lng,
        accuracy: data.accuracy
      };
    }
  } catch (error) {
    console.log('Erro na geolocalização:', error);
  }
  return null;
}

// Função para processar scans e adicionar coordenadas
async function processScansWithCoordinates(bikeId: string) {
  const sessionsRef = db.ref(`bikes/${bikeId}/sessions`);
  const snapshot = await sessionsRef.once('value');
  const sessions = snapshot.val();
  
  if (!sessions) return;

  for (const [sessionId, sessionData] of Object.entries(sessions)) {
    const session = sessionData as any;
    
    if (!session.scans) continue;
    
    console.log(`Processando sessão ${sessionId}...`);
    
    for (let i = 0; i < session.scans.length; i++) {
      const scan = session.scans[i];
      
      // Pula se já foi convertido
      if (scan.converted) continue;
      
      const timestamp = scan[0];
      const networks = scan[2]; // Array de redes
      
      console.log(`  Scan ${i} (${timestamp})...`);
      
      // Converte redes WiFi em coordenadas
      const coordinates = await convertWifiToCoordinates(networks);
      
      // Atualiza o scan com coordenadas e flag
      await sessionsRef.child(`${sessionId}/scans/${i}`).update({
        converted: true,
        coordinates: coordinates,
        processedAt: Date.now()
      });
      
      // Delay para não sobrecarregar a API
      await new Promise(resolve => setTimeout(resolve, 1000));
    }
  }
}

// Comandos do bot
bot.start((ctx) => {
  const welcomeMessage = "🚴 *Bot de Monitoramento de Bicicletas*\n\n" +
    "Comandos:\n" +
    "/status [bike] - Status da bike\n" +
    "/rota [bike] - Última rota\n" +
    "/converter [bike] - Converter WiFi em GPS\n" +
    "/bikes - Listar bikes";

  ctx.reply(welcomeMessage, { parse_mode: "Markdown" });
});

bot.hears(/bota pra rodar/i, (ctx) => {
  ctx.reply("Bot");
});

bot.command("bikes", async (ctx) => {
  try {
    const snapshot = await db.ref("bikes").once("value");
    const bikes = snapshot.val();

    if (!bikes) {
      ctx.reply("❌ Nenhuma bike encontrada");
      return;
    }

    const bikeIds = Object.keys(bikes);
    let message = `🚴 *Bikes Monitoradas*\n\n`;
    
    bikeIds.forEach(id => {
      if (id === "intenso") {
        message += `• ${id.toUpperCase()} - Bike de teste/desenvolvimento\n`;
      } else {
        message += `• ${id.toUpperCase()}\n`;
      }
    });
    
    message += "\nPara mais informações:\n";
    bikeIds.forEach(id => {
      message += `\`/status ${id}\` - Ver status atual\n`;
      message += `\`/rota ${id}\` - Ver última rota\n`;
    });

    ctx.reply(message, { parse_mode: "Markdown" });
  } catch (error) {
    ctx.reply("❌ Erro ao listar bikes");
  }
});

bot.command("status", async (ctx) => {
  const bikeId = ctx.message.text.split(" ")[1];
  if (!bikeId) {
    ctx.reply("❌ Especifique o ID da bike.\nExemplo: /status intenso");
    return;
  }

  try {
    const snapshot = await db.ref(`bikes/${bikeId}/sessions`)
      .orderByKey().limitToLast(1).once("value");
    const sessions = snapshot.val();

    if (!sessions) {
      ctx.reply(`❌ Bike ${bikeId} não encontrada`);
      return;
    }

    const sessionId = Object.keys(sessions)[0];
    const session = sessions[sessionId];

    const isActive = !session.end;
    const startDate = new Date(session.start * 1000)
      .toLocaleString("pt-BR");

    let message = `🚴 Bike ${bikeId.toUpperCase()}\n\n` +
                 `📍 Status: ${isActive ? "🚀 Ativa" : "✅ Finalizada"}\n` +
                 `🏷️ Sessão: ${sessionId}\n` +
                 `🚀 Início: ${startDate}\n`;

    if (session.scans) {
      message += `📊 Scans: ${session.scans.length}\n`;
    }

    ctx.reply(message);
  } catch (error) {
    ctx.reply("❌ Erro ao buscar status");
  }
});

bot.command("converter", async (ctx) => {
  const bikeId = ctx.message.text.split(" ")[1];
  if (!bikeId) {
    ctx.reply("❌ Especifique o ID da bike.\nExemplo: /converter intenso");
    return;
  }

  ctx.reply(`🔄 Processando scans de ${bikeId}...`);
  
  try {
    await processScansWithCoordinates(bikeId);
    ctx.reply(`✅ Scans de ${bikeId} processados com coordenadas!`);
  } catch (error) {
    ctx.reply("❌ Erro ao processar scans");
  }
});

bot.command("rota", async (ctx) => {
  const bikeId = ctx.message.text.split(" ")[1];
  if (!bikeId) {
    ctx.reply("❌ Especifique o ID da bike.\nExemplo: /rota economico");
    return;
  }

  try {
    const sessionsSnapshot = await db.ref(`bikes/${bikeId}/sessions`).once("value");
    const sessions = sessionsSnapshot.val();

    if (!sessions) {
      ctx.reply(`❌ Nenhuma sessão encontrada para ${bikeId}`);
      return;
    }

    // Pega a sessão mais recente
    const sessionIds = Object.keys(sessions).sort().reverse();
    const latestSessionId = sessionIds[0];
    const latestSession = sessions[latestSessionId];

    if (!latestSession.scans || latestSession.scans.length < 2) {
      ctx.reply(`❌ Poucos scans na sessão (${latestSession.scans?.length || 0})`);
      return;
    }

    const scans = latestSession.scans;
    
    // Calcula distância usando coordenadas GPS ou RSSI
    let totalDistance = 0;
    let movementPoints = 0;
    let convertedScans = 0;
    
    // Conta scans convertidos
    scans.forEach((scan: any) => {
      if (scan.converted && scan.coordinates) convertedScans++;
    });
    
    for (let i = 1; i < scans.length; i++) {
      const prev = scans[i - 1];
      const curr = scans[i];
      
      // Usa coordenadas GPS se disponível
      if (prev.coordinates && curr.coordinates) {
        const distance = calculateGPSDistance(
          prev.coordinates.lat, prev.coordinates.lng,
          curr.coordinates.lat, curr.coordinates.lng
        );
        
        if (distance > 5) {
          totalDistance += distance;
          movementPoints++;
        }
      } else {
        // Fallback para método RSSI usando novo formato
        const prevNetworks = prev[2] || []; // Array de redes
        const currNetworks = curr[2] || [];
        
        const commonNetworks = currNetworks.filter((net: any) => 
          prevNetworks.some((pNet: any) => pNet[0] === net[0]) // Compara SSID
        );
        
        if (commonNetworks.length > 0) {
          let rssiVariation = 0;
          commonNetworks.forEach((net: any) => {
            const prevNet = prevNetworks.find((pNet: any) => pNet[0] === net[0]);
            if (prevNet) {
              rssiVariation += Math.abs(net[2] - prevNet[2]); // Compara RSSI
            }
          });
          
          const estimatedDistance = (rssiVariation / commonNetworks.length) * 8;
          
          if (estimatedDistance > 5) {
            totalDistance += estimatedDistance;
            movementPoints++;
          }
        }
      }
    }

    const startTime = new Date((latestSession.start + scans[0][0]/1000) * 1000).toLocaleString("pt-BR");
    const endTime = new Date((latestSession.start + scans[scans.length-1][0]/1000) * 1000).toLocaleString("pt-BR");
    const duration = Math.round((scans[scans.length-1][0] - scans[0][0]) / 60000);
    
    // Redes únicas detectadas
    const allNetworks = new Set();
    scans.forEach((scan: any) => {
      if (scan[2]) {
        scan[2].forEach((net: any) => allNetworks.add(net[0])); // SSID
      }
    });

    let message = `🗺️ *Rota ${bikeId.toUpperCase()}*\n\n` +
                 `🏷️ Sessão: ${latestSessionId}\n` +
                 `📊 Scans: ${scans.length}\n` +
                 `🌐 Convertidos: ${convertedScans}/${scans.length}\n` +
                 `📏 Distância: ${totalDistance.toFixed(0)}m\n` +
                 `⏱️ Duração: ${duration} min\n` +
                 `📡 Redes detectadas: ${allNetworks.size}\n` +
                 `🚀 Início: ${startTime}\n` +
                 `🏁 Fim: ${endTime}\n\n`;
    
    if (movementPoints > 0) {
      message += `✅ Movimento detectado em ${movementPoints} pontos`;
    } else {
      message += `⚠️ Pouco movimento detectado - bike pode ter ficado parada`;
    }

    ctx.reply(message, { parse_mode: "Markdown" });
  } catch (error) {
    ctx.reply("❌ Erro ao calcular rota");
  }
});

// Inicia o bot
bot.launch().then(() => {
  console.log('🚀 Bot rodando localmente...');
  console.log('✅ Conectado ao Firebase');
  console.log('📱 Aguardando mensagens...');
});

// Graceful stop
process.once('SIGINT', () => bot.stop('SIGINT'));
process.once('SIGTERM', () => bot.stop('SIGTERM'));