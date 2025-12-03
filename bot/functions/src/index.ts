import * as functions from "firebase-functions";
import * as admin from "firebase-admin";
import {Telegraf} from "telegraf";
import axios from "axios";

// Inicialização do Firebase Admin
if (!admin.apps.length) {
  admin.initializeApp();
}

const db = admin.database();

const BOT_TOKEN = functions.config().telegram?.bot_token || "";
const ADMIN_CHAT_ID = functions.config().admin?.chat_id || "";
const GOOGLE_API_KEY = functions.config().google?.api_key || "";

console.log("🔧 Configurações carregadas:", {
  botToken: BOT_TOKEN ? "✅" : "❌",
  adminChatId: ADMIN_CHAT_ID ? "✅" : "❌",
  googleApiKey: GOOGLE_API_KEY ? "✅" : "❌"
});

const bot = new Telegraf(BOT_TOKEN);

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
    // Detectar formato dos dados
    const isArrayFormat = Array.isArray(networks[0]);
    
    const wifiAccessPoints = networks.map(net => {
      if (isArrayFormat) {
        // Formato array: [ssid, bssid, rssi, channel]
        return {
          macAddress: net[1],
          signalStrength: net[2], 
          channel: net[3]
        };
      } else {
        // Formato objeto: {ssid, bssid, rssi, channel}
        return {
          macAddress: net.bssid,
          signalStrength: net.rssi,
          channel: net.channel
        };
      }
    });

    const response = await axios.post(
      `https://www.googleapis.com/geolocation/v1/geolocate?key=${GOOGLE_API_KEY}`,
      { wifiAccessPoints }
    );

    if (response.status === 200 && response.data.location) {
      return {
        lat: response.data.location.lat,
        lng: response.data.location.lng,
        accuracy: response.data.accuracy
      };
    }
  } catch (error) {
    console.log('Erro na geolocalização:', error.response?.data || error.message);
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
      
      // Pula se já tem coordenadas
      if (scan.coordinates) continue;
      
      // Detectar formato dos dados
      let networks;
      if (Array.isArray(scan)) {
        // Formato array: [timestamp, realTime, networks]
        networks = scan[2];
      } else {
        // Formato objeto: {networks, timestamp, etc}
        networks = scan.networks;
      }
      
      if (!networks || networks.length === 0) continue;
      
      console.log(`  Scan ${i} - ${networks.length} redes...`);
      
      // Converte redes WiFi em coordenadas
      const coordinates = await convertWifiToCoordinates(networks);
      
      // Atualiza o scan com coordenadas
      if (coordinates) {
        await sessionsRef.child(`${sessionId}/scans/${i}/coordinates`).set(coordinates);
        console.log(`    ✅ Coordenadas: ${coordinates.lat}, ${coordinates.lng}`);
      } else {
        console.log(`    ❌ Não foi possível obter coordenadas`);
      }
      
      // Delay para não sobrecarregar a API
      await new Promise(resolve => setTimeout(resolve, 1000));
    }
  }
}

/**
 * Send notification to admin
 * @param {string} message Message to send
 */
async function sendNotification(message: string): Promise<void> {
  try {
    await bot.telegram.sendMessage(ADMIN_CHAT_ID, message);
  } catch (error) {
    console.error("Erro notificação:", error);
  }
}

export const onNewSession = functions.database
  .ref("/bikes/{bikeId}/sessions/{sessionId}")
  .onCreate(async (snapshot, context) => {
    const bikeId = context.params.bikeId;
    const sessionId = context.params.sessionId;
    const sessionData = snapshot.val();

    console.log(`📝 Nova sessão criada: ${bikeId}/${sessionId}`, sessionData);

    const startDate = new Date(sessionData.start * 1000)
      .toLocaleString("pt-BR");

    const message = `🚴 Bike ${bikeId.toUpperCase()}\n` +
                   `🎆 NOVA SESSÃO INICIADA\n` +
                   `📅 ${startDate}\n` +
                   `🏷️ Sessão: ${sessionId}\n` +
                   `⚙️ Modo: ${sessionData.mode || "normal"}`;

    await sendNotification(message);
  });

export const onNewScan = functions.database
  .ref("/bikes/{bikeId}/sessions/{sessionId}/scans/{scanIndex}")
  .onCreate(async (snapshot, context) => {
    const bikeId = context.params.bikeId;
    const scanData = snapshot.val();
    const [timestamp, networks] = scanData;

    console.log(`📡 Novo scan: ${bikeId}`, { timestamp, networksCount: networks.length });

    const date = new Date(timestamp * 1000).toLocaleString("pt-BR");
    const networksCount = networks.length;
    const strongNetworks = networks.filter(
      ([, , rssi]: [string, string, number, number]) => rssi > -60
    ).length;

    let message = `🚴 Bike ${bikeId.toUpperCase()}\n` +
                 `📅 ${date}\n` +
                 `📡 ${networksCount} redes WiFi detectadas\n` +
                 `💪 ${strongNetworks} redes com sinal forte\n`;

    const topNetworks = networks
      .map(([ssid, , rssi]: [string, string, number, number]) => 
        ({ssid, rssi}))
      .sort((a: {rssi: number}, b: {rssi: number}) => b.rssi - a.rssi)
      .slice(0, 3);

    message += "\n🔝 Redes mais fortes:\n";
    topNetworks.forEach(
      (network: {ssid: string; rssi: number}, i: number) => {
        message += `${i + 1}. ${network.ssid} (${network.rssi}dBm)\n`;
      }
    );

    await sendNotification(message);
  });

bot.start((ctx) => {
  console.log("📱 Comando /start recebido de:", ctx.from?.username || ctx.from?.id);
  const welcomeMessage = "🚴 *Bot de Monitoramento de Bicicletas*\n\n" +
    "Comandos:\n" +
    "/status [bike] - Status da bike\n" +
    "/rota [bike] - Última rota\n" +
    "/converter [bike] - Converter WiFi em GPS\n" +
    "/bikes - Listar bikes";

  ctx.reply(welcomeMessage, { parse_mode: "Markdown" });
});

bot.hears(/bota pra rodar/i, (ctx) => {
  console.log("🎯 Mensagem 'bota pra rodar' detectada:", ctx.message.text);
  ctx.reply("Bot");
});

bot.command("converter", async (ctx) => {
  const bikeId = ctx.message.text.split(" ")[1];
  if (!bikeId) {
    ctx.reply("❌ Especifique o ID da bike.\nExemplo: /converter intenso");
    return;
  }

  ctx.reply("🔄 Processando scans e gerando coordenadas...");

  try {
    const snapshot = await db.ref(`bikes/${bikeId}/sessions`).once("value");
    const sessions = snapshot.val();

    if (!sessions) {
      ctx.reply(`❌ Bike ${bikeId} não encontrada`);
      return;
    }

    let totalProcessed = 0;
    let totalCoordinates = 0;

    for (const [sessionId, session] of Object.entries(sessions)) {
      if (!session.scans) continue;

      for (const [scanIndex, scanData] of Object.entries(session.scans)) {
        const [timestamp, networks] = scanData;
        
        // Pula se já tem coordenadas
        if (scanData.coordinates) continue;

        const location = await GeolocationService.getLocationFromWifi(networks);
        
        if (location) {
          await db.ref(`bikes/${bikeId}/sessions/${sessionId}/scans/${scanIndex}/coordinates`).set({
            lat: location.latitude,
            lng: location.longitude,
            accuracy: location.accuracy,
            timestamp: Date.now()
          });
          totalCoordinates++;
        }
        
        totalProcessed++;
      }
    }

    ctx.reply(`✅ Processamento concluído!\n📊 ${totalProcessed} scans processados\n📍 ${totalCoordinates} coordenadas geradas`);
  } catch (error) {
    console.error("Erro no comando converter:", error);
    ctx.reply("❌ Erro ao processar scans");
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

    const sessionIds = Object.keys(sessions).sort().reverse();
    const latestSessionId = sessionIds[0];
    const latestSession = sessions[latestSessionId];

    if (!latestSession.scans || latestSession.scans.length < 2) {
      ctx.reply(`❌ Poucos scans na sessão (${latestSession.scans?.length || 0})`);
      return;
    }

    const scans = latestSession.scans;
    let totalDistance = 0;
    let movementPoints = 0;
    let convertedScans = 0;
    
    scans.forEach((scan: any) => {
      if (scan.converted && scan.coordinates) convertedScans++;
    });
    
    for (let i = 1; i < scans.length; i++) {
      const prev = scans[i - 1];
      const curr = scans[i];
      
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
        const prevNetworks = prev[2] || [];
        const currNetworks = curr[2] || [];
        
        const commonNetworks = currNetworks.filter((net: any) => 
          prevNetworks.some((pNet: any) => pNet[0] === net[0])
        );
        
        if (commonNetworks.length > 0) {
          let rssiVariation = 0;
          commonNetworks.forEach((net: any) => {
            const prevNet = prevNetworks.find((pNet: any) => pNet[0] === net[0]);
            if (prevNet) {
              rssiVariation += Math.abs(net[2] - prevNet[2]);
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
    
    const allNetworks = new Set();
    scans.forEach((scan: any) => {
      if (scan[2]) {
        scan[2].forEach((net: any) => allNetworks.add(net[0]));
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

export const telegramBot = functions.https.onRequest(async (req, res) => {
  try {
    console.log("🔄 Webhook recebido:", JSON.stringify(req.body, null, 2));
    await bot.handleUpdate(req.body);
    res.status(200).send("OK");
  } catch (error) {
    console.error("❌ Webhook error:", error);
    res.status(500).send("Error");
  }
});