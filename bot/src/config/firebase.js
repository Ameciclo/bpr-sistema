const admin = require('firebase-admin');

class FirebaseService {
  // A05: registro de listeners por sessão — evita listeners duplicados que
  // nunca são removidos (cada sessão assinada uma única vez)
  constructor() {
    this.db = null;
    this.sessionListeners = new Set();
    this.init();
  }

  init() {
    try {
      const serviceAccount = {
        type: "service_account",
        project_id: process.env.FIREBASE_PROJECT_ID,
        private_key: process.env.FIREBASE_PRIVATE_KEY?.replace(/\\n/g, '\n'),
        client_email: process.env.FIREBASE_CLIENT_EMAIL,
      };

      admin.initializeApp({
        credential: admin.credential.cert(serviceAccount),
        databaseURL: process.env.FIREBASE_DATABASE_URL
      });

      this.db = admin.database();
      console.log('✅ Firebase conectado com sucesso');
    } catch (error) {
      console.error('❌ Erro ao conectar Firebase:', error.message);
    }
  }

  // Escutar novas sessões de bikes (projeção v2 do normalizador)
  listenToNewSessions(callback) {
    if (!this.db) return;

    const bikesRef = this.db.ref('bikes');
    bikesRef.on('child_changed', (snapshot) => {
      const bikeId = snapshot.key;
      const bikeData = snapshot.val();

      // A02/A05: sessões v2 (sessions_v2) têm chave estável {sid}-{salt};
      // a mais recente é a maior chave lexicográfica, não a última inserida
      if (bikeData.sessions_v2) {
        const keys = Object.keys(bikeData.sessions_v2).sort();
        const latestSession = keys[keys.length - 1];
        if (latestSession) {
          callback(bikeId, latestSession, bikeData.sessions_v2[latestSession]);
        }
      }
    });
  }

  // Escutar mudanças em sessões específicas (scans em tempo real)
  listenToSessionScans(bikeId, sessionId, callback) {
    if (!this.db) return;

    // A05: uma assinatura por sessão — re-chamadas não acumulam listeners
    const listenerKey = `${bikeId}/${sessionId}`;
    if (this.sessionListeners.has(listenerKey)) return;
    this.sessionListeners.add(listenerKey);

    const sessionRef = this.db.ref(`bikes/${bikeId}/sessions_v2/${sessionId}/scans`);
    sessionRef.on('child_added', callback);
  }

  // Escutar conexões BLE (chegadas/saídas da base)
  listenToBikeConnections(callback) {
    if (!this.db) return;
    
    const bikesRef = this.db.ref('bikes');
    bikesRef.on('child_changed', (snapshot) => {
      const bikeId = snapshot.key;
      const bikeData = snapshot.val();
      
      // Verificar se há novas conexões
      if (bikeData.sessions) {
        const sessions = Object.values(bikeData.sessions);
        const latestSession = sessions[sessions.length - 1];
        
        if (latestSession.connections) {
          const connections = latestSession.connections;
          const latestConnection = connections[connections.length - 1];
          
          if (latestConnection) {
            const [time, event, base, ip] = latestConnection;
            callback(bikeId, { time, event, base, ip });
          }
        }
      }
    });
  }

  // Buscar dados completos de uma bike
  async getBikeData(bikeId) {
    if (!this.db) return null;
    
    try {
      const snapshot = await this.db.ref(`bikes/${bikeId}`).once('value');
      return snapshot.val();
    } catch (error) {
      console.error('Erro ao buscar dados da bike:', error);
      return null;
    }
  }

  // Buscar última sessão de uma bike (projeção v2)
  async getLastSession(bikeId) {
    if (!this.db) return null;

    try {
      const snapshot = await this.db.ref(`bikes/${bikeId}/sessions_v2`)
        .orderByKey()
        .limitToLast(1)
        .once('value');

      const sessions = snapshot.val();
      if (!sessions) return null;

      const sessionId = Object.keys(sessions)[0];
      return {
        sessionId,
        ...sessions[sessionId]
      };
    } catch (error) {
      console.error('Erro ao buscar sessão:', error);
      return null;
    }
  }

  // Buscar redes conhecidas de uma bike
  async getBikeNetworks(bikeId) {
    if (!this.db) return {};
    
    try {
      const snapshot = await this.db.ref(`bikes/${bikeId}/networks`).once('value');
      return snapshot.val() || {};
    } catch (error) {
      console.error('Erro ao buscar redes:', error);
      return {};
    }
  }

  // Buscar todas as bases
  async getAllBases() {
    if (!this.db) return {};
    
    try {
      const snapshot = await this.db.ref('bases').once('value');
      return snapshot.val() || {};
    } catch (error) {
      console.error('Erro ao buscar bases:', error);
      return {};
    }
  }

  // Buscar estatísticas públicas
  async getPublicStats() {
    if (!this.db) return {};
    
    try {
      const snapshot = await this.db.ref('public_stats').once('value');
      return snapshot.val() || {};
    } catch (error) {
      console.error('Erro ao buscar estatísticas:', error);
      return {};
    }
  }
  // Escutar mudanças de bateria.
  // A04: DTO de bateria separa percentual, tensão e carregamento — aceita o
  // array do firmware [ts_ms, percent] e objetos {percent, voltage, charging},
  // sem converter percentual em tensão (e vice-versa) implicitamente
  listenToBatteryChanges(callback) {
    if (!this.db) return;

    const bikesRef = this.db.ref('bikes');
    bikesRef.on('child_changed', (snapshot) => {
      const bikeId = snapshot.key;
      const bikeData = snapshot.val();

      if (bikeData.sessions_v2) {
        const keys = Object.keys(bikeData.sessions_v2).sort();
        const activeSession = bikeData.sessions_v2[keys[keys.length - 1]];

        if (activeSession && Array.isArray(activeSession.battery)) {
          const readings = activeSession.battery
            .map((r) => {
              if (Array.isArray(r)) {
                return { timestamp: r[0], percent: r[1], voltage: null, charging: null };
              }
              return {
                timestamp: r.ts_ms ?? r.timestamp ?? null,
                percent: r.percent ?? null,
                voltage: r.voltage ?? null,
                charging: r.charging ?? null
              };
            })
            .filter((r) => r.percent !== null || r.voltage !== null);

          const latestReading = readings[readings.length - 1];
          if (latestReading) {
            callback(bikeId, latestReading);
          }
        }
      }
    });
  }
}

module.exports = new FirebaseService();