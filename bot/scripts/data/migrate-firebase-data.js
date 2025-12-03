#!/usr/bin/env node

require('dotenv').config();
const admin = require('firebase-admin');
const fs = require('fs');

class FirebaseMigration {
  constructor() {
    this.db = null;
    this.stats = {
      bikesProcessed: 0,
      sessionsProcessed: 0,
      scansConverted: 0,
      errors: []
    };
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
      console.log('✅ Firebase conectado');
    } catch (error) {
      console.error('❌ Erro Firebase:', error.message);
      process.exit(1);
    }
  }

  convertCompactToLabeled(compactArray) {
    if (!Array.isArray(compactArray) || compactArray.length < 3) {
      throw new Error('Formato inválido');
    }

    return {
      timestamp: compactArray[0],
      realTime: compactArray[1],
      networks: compactArray[2].map(net => ({
        ssid: net[0],
        bssid: net[1],
        rssi: net[2],
        channel: net[3]
      }))
    };
  }

  isCompactFormat(scan) {
    return Array.isArray(scan) && scan.length >= 3 && Array.isArray(scan[2]);
  }

  async createBackup() {
    const timestamp = new Date().toISOString().slice(0, 10).replace(/-/g, '');
    const backupFile = `backup_${timestamp}.json`;
    
    try {
      const snapshot = await this.db.ref('bikes').once('value');
      fs.writeFileSync(backupFile, JSON.stringify(snapshot.val(), null, 2));
      console.log(`✅ Backup salvo: ${backupFile}`);
      return backupFile;
    } catch (error) {
      console.error('❌ Erro no backup:', error.message);
      throw error;
    }
  }

  async migrateBike(bikeId, bikeData) {
    console.log(`🚴 Processando bike: ${bikeId}`);
    
    if (!bikeData.sessions) {
      console.log(`  ⚠️  Sem sessões`);
      return;
    }

    for (const [sessionId, sessionData] of Object.entries(bikeData.sessions)) {
      await this.migrateSession(bikeId, sessionId, sessionData);
    }
    
    this.stats.bikesProcessed++;
  }

  async migrateSession(bikeId, sessionId, sessionData) {
    if (!sessionData.scans || !Array.isArray(sessionData.scans)) {
      return;
    }

    const convertedScans = [];
    let hasCompactData = false;

    for (const scan of sessionData.scans) {
      if (this.isCompactFormat(scan)) {
        try {
          convertedScans.push(this.convertCompactToLabeled(scan));
          hasCompactData = true;
          this.stats.scansConverted++;
        } catch (error) {
          this.stats.errors.push(`${bikeId}/${sessionId}: ${error.message}`);
        }
      } else {
        convertedScans.push(scan);
      }
    }

    if (hasCompactData) {
      await this.db.ref(`bikes/${bikeId}/sessions/${sessionId}/scans`).set(convertedScans);
      console.log(`  ✅ Sessão: ${sessionId} - ${convertedScans.length} scans`);
    }

    this.stats.sessionsProcessed++;
  }

  async run() {
    console.log('🚀 Iniciando migração Firebase\n');

    try {
      // Backup
      await this.createBackup();

      // Buscar todas as bikes
      const snapshot = await this.db.ref('bikes').once('value');
      const bikes = snapshot.val();

      if (!bikes) {
        console.log('❌ Nenhuma bike encontrada');
        return;
      }

      // Migrar cada bike
      for (const [bikeId, bikeData] of Object.entries(bikes)) {
        await this.migrateBike(bikeId, bikeData);
      }

      // Relatório final
      console.log('\n📊 RELATÓRIO FINAL:');
      console.log(`✅ Bikes processadas: ${this.stats.bikesProcessed}`);
      console.log(`✅ Sessões processadas: ${this.stats.sessionsProcessed}`);
      console.log(`✅ Scans convertidos: ${this.stats.scansConverted}`);
      
      if (this.stats.errors.length > 0) {
        console.log(`❌ Erros: ${this.stats.errors.length}`);
        this.stats.errors.forEach(error => console.log(`  - ${error}`));
      }

    } catch (error) {
      console.error('❌ Erro na migração:', error.message);
    } finally {
      process.exit(0);
    }
  }
}

// Executar migração
const migration = new FirebaseMigration();
migration.run();