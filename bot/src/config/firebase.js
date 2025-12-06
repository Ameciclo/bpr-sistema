const admin = require('firebase-admin');

class FirebaseService {
  constructor() {
    this.db = null;
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

  // Escutar novas sessões de bikes
  listenToNewSessions(callback) {
    if (!this.db) return;
    
    const bikesRef = this.db.ref('bikes');
    bikesRef.on('child_changed', (snapshot) => {
      const bikeId = snapshot.key;
      const bikeData = snapshot.val();
      
      // Verificar se há nova sessão
      if (bikeData.sessions) {
        const sessions = Object.keys(bikeData.sessions);
        const latestSession = sessions[sessions.length - 1];
        callback(bikeId, latestSession, bikeData.sessions[latestSession]);
      }
    });
  }

  // Escutar mudanças em sessões específicas (scans em tempo real)
  listenToSessionScans(bikeId, sessionId, callback) {
    if (!this.db) return;
    
    const sessionRef = this.db.ref(`bikes/${bikeId}/sessions/${sessionId}/scans`);
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

  // Buscar última sessão de uma bike
  async getLastSession(bikeId) {
    if (!this.db) return null;
    
    try {
      const snapshot = await this.db.ref(`bikes/${bikeId}/sessions`)
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
  // Escutar mudanças de bateria
  listenToBatteryChanges(callback) {
    if (!this.db) return;
    
    const bikesRef = this.db.ref('bikes');
    bikesRef.on('child_changed', (snapshot) => {
      const bikeId = snapshot.key;
      const bikeData = snapshot.val();
      
      if (bikeData.sessions) {
        const sessions = Object.values(bikeData.sessions);
        const activeSession = sessions.find(s => !s.end);
        
        if (activeSession && activeSession.battery) {
          const batteryReadings = activeSession.battery;
          const latestReading = batteryReadings[batteryReadings.length - 1];
          
          if (latestReading) {
            const [timestamp, voltage, charging] = latestReading;
            callback(bikeId, { voltage, charging: !!charging, timestamp });
          }
        }
      }
    });
  }
}

module.exports = new FirebaseService();