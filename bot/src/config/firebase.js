const admin = require('firebase-admin');

// N10: comparação NUMÉRICA de chaves "{sid}-{salt}" — a lexicográfica
// ordena "9999-1" DEPOIS de "10000-1" e não reflete ordem temporal
function sessionKeyComparator(a, b) {
  const pa = String(a).split('-');
  const pb = String(b).split('-');
  const sidA = Number(pa[0]) || 0;
  const sidB = Number(pb[0]) || 0;
  if (sidA !== sidB) return sidA - sidB;
  return (Number(pa[1]) || 0) - (Number(pb[1]) || 0);
}

class FirebaseService {
  // A05: registro de listeners por sessão — evita listeners duplicados que
  // nunca são removidos (cada sessão assinada uma única vez)
  // N10: cursor de sessões já notificadas + referência ativa por bike — a
  // assinatura anterior é DESANEXADA quando a próxima sessão é assinada
  constructor() {
    this.db = null;
    this.sessionListeners = new Map(); // bikeId -> {key, ref}
    this.notifiedSessions = new Map(); // bikeId -> Set(sessionKey)
    this.processedTrips = new Map(); // bikeId -> Set(tripSeq)
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

      // A02/A05/N10: sessões v2 têm chave estável {sid}-{salt}; a seleção é
      // NUMÉRICA (a lexicográfica escolhia sessão errada) e com CURSOR — só
      // notifica sessões ainda não vistas, sem reanunciar história no restart
      if (bikeData.sessions_v2) {
        const seen = this.notifiedSessions.get(bikeId) || new Set();
        const unseen = Object.keys(bikeData.sessions_v2)
          .filter((key) => !seen.has(key))
          .sort(sessionKeyComparator);

        if (unseen.length > 0) {
          const latestSession = unseen[unseen.length - 1];
          for (const key of unseen) seen.add(key);
          this.notifiedSessions.set(bikeId, seen);
          callback(bikeId, latestSession, bikeData.sessions_v2[latestSession]);
        }
      }
    });
  }

  // Escutar mudanças em sessões específicas (scans em tempo real)
  listenToSessionScans(bikeId, sessionId, callback) {
    if (!this.db) return;

    // A05/N10: UMA assinatura ativa por bike — a sessão anterior é
    // desanexada (listeners acumulados por sessão nunca eram removidos)
    const active = this.sessionListeners.get(bikeId);
    if (active) {
      if (active.key === `${bikeId}/${sessionId}`) return; // já assinada
      active.ref.off('child_added');
    }

    const sessionRef = this.db.ref(`bikes/${bikeId}/sessions_v2/${sessionId}/scans`);
    sessionRef.on('child_added', callback);
    this.sessionListeners.set(bikeId, { key: `${bikeId}/${sessionId}`, ref: sessionRef });
  }

  // Patch de horários: chegadas de viagem — o trip com chegada civil válida
  // conclui a viagem pela hora REAL do evento (não pela hora de upload)
  listenToTripArrivals(callback) {
    if (!this.db) return;

    const bikesRef = this.db.ref('bikes');
    bikesRef.on('child_changed', (snapshot) => {
      const bikeId = snapshot.key;
      const bikeData = snapshot.val();
      if (!bikeData || !bikeData.trips) return;

      const seen = this.processedTrips.get(bikeId) || new Set();
      for (const [tripSeq, trip] of Object.entries(bikeData.trips)) {
        if (!trip || seen.has(tripSeq)) continue;
        seen.add(tripSeq);
        const recon = trip.reconstruction;
        const arrival = recon && recon.arrival;
        // Só conclui com chegada civil VÁLIDA e duração consistente;
        // pendências permanecem visíveis sem inventar conclusão (H10)
        if (arrival && arrival.valid && Number.isFinite(Number(arrival.utc_ms)) &&
            Number(arrival.utc_ms) > 0 && recon.quality === 'ok') {
          const baseId = (trip.events || []).find((e) => e.kind === 'arrival');
          callback(bikeId, Number(tripSeq), Number(arrival.utc_ms),
            baseId ? baseId.base_id : 'unknown');
        }
      }
      this.processedTrips.set(bikeId, seen);
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
        .once('value');

      const sessions = snapshot.val();
      if (!sessions) return null;

      // N10: recência ancorada no SERVIDOR (ingest_ts da montagem) — a chave
      // {sid}-{salt} não é cronológica entre boots; fallback numérico
      const keys = Object.keys(sessions);
      const best = keys.reduce((acc, key) => {
        if (!acc) return key;
        const accTs = Number(sessions[acc]?.ingest_ts) || 0;
        const keyTs = Number(sessions[key]?.ingest_ts) || 0;
        if (accTs !== keyTs) return keyTs > accTs ? key : acc;
        return sessionKeyComparator(acc, key) < 0 ? key : acc;
      }, null);

      if (!best) return null;
      return {
        sessionId: best,
        ...sessions[best]
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
        // N10: seleção numérica/ingest_ts (a lexicográfica escolhia errado)
        const keys = Object.keys(bikeData.sessions_v2);
        const activeKey = keys.reduce((acc, key) => {
          if (!acc) return key;
          const accTs = Number(bikeData.sessions_v2[acc]?.ingest_ts) || 0;
          const keyTs = Number(bikeData.sessions_v2[key]?.ingest_ts) || 0;
          if (accTs !== keyTs) return keyTs > accTs ? key : acc;
          return sessionKeyComparator(acc, key) < 0 ? key : acc;
        }, null);
        const activeSession = activeKey
          ? bikeData.sessions_v2[activeKey] : null;

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