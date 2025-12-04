const chalk = require('chalk');

class Central {
  constructor(baseId, firebase) {
    this.baseId = baseId;
    this.firebase = firebase;
    this.config = null;
    this.connectedBikes = new Set();
    this.ledState = 'off';
    this.uptime = 0;
  }

  async boot() {
    console.log(chalk.blue(`🔄 [${this.baseId}] Inicializando central...`));
    this.ledState = 'booting';
    this.log('LED: Piscar rápido (inicializando)');
    
    await this.sleep(1000);
    
    // Simula conexão WiFi
    this.log('Conectando ao WiFi...');
    await this.sleep(2000);
    this.log('✅ WiFi conectado');
    
    // Simula sincronização de tempo
    this.log('Sincronizando NTP...');
    await this.sleep(1000);
    this.log('✅ Horário sincronizado');
  }

  async loadConfig() {
    this.log('📥 Carregando configurações do Firebase...');
    
    // Simula download da config
    await this.sleep(1500);
    
    this.config = await this.firebase.getCentralConfig(this.baseId);
    this.log(`✅ Configuração carregada: ${JSON.stringify(this.config, null, 2)}`);
  }

  async startBLE() {
    this.log('📡 Iniciando BLE...');
    this.ledState = 'ble_ready';
    this.log('LED: Piscar lento (BLE ativo)');
    
    await this.sleep(1000);
    this.log('✅ BLE ativo - Aguardando bikes...');
    
    // Inicia heartbeat
    this.startHeartbeat();
  }

  async onBikeConnected(bikeId) {
    this.connectedBikes.add(bikeId);
    this.ledState = 'bike_arrived';
    this.log(`🚲 Bike ${bikeId} conectada`);
    this.log('LED: 3 piscadas rápidas (bike chegou)');
    
    await this.firebase.updateBikeStatus(bikeId, 'connected', this.baseId);
    
    // Volta ao estado normal após indicação
    setTimeout(() => {
      this.ledState = 'normal';
    }, 2000);
  }

  async onBikeLeft(bikeId) {
    this.connectedBikes.delete(bikeId);
    this.ledState = 'bike_left';
    this.log(`🚲 Bike ${bikeId} saiu da base`);
    this.log('LED: 1 piscada longa (bike saiu)');
    
    await this.firebase.updateBikeStatus(bikeId, 'active', null);
    
    setTimeout(() => {
      this.ledState = 'normal';
    }, 2000);
  }

  async onLowBattery(bikeId, voltage) {
    this.log(`🔋 Alerta: Bike ${bikeId} com bateria baixa (${voltage}V)`);
    await this.firebase.createAlert('battery_low', bikeId, { voltage });
  }

  async syncWithFirebase() {
    this.ledState = 'syncing';
    this.log('🔄 Sincronizando com Firebase...');
    this.log('LED: Piscar médio (sincronizando)');
    
    try {
      // Simula upload de dados
      await this.sleep(2000);
      
      const data = {
        bikes_connected: this.connectedBikes.size,
        uptime: this.uptime,
        last_sync: Date.now()
      };
      
      await this.firebase.updateBaseStatus(this.baseId, data);
      this.log('✅ Sincronização completa');
      
    } catch (error) {
      this.ledState = 'error';
      this.log('❌ Erro na sincronização');
      this.log('LED: Piscar muito rápido (erro)');
    }
    
    setTimeout(() => {
      this.ledState = 'normal';
    }, 1000);
  }

  startHeartbeat() {
    setInterval(async () => {
      this.uptime += 30;
      
      // LED de contagem a cada 30s
      if (this.connectedBikes.size > 0) {
        this.log(`LED: ${this.connectedBikes.size} piscadas (${this.connectedBikes.size} bikes)`);
      }
      
      // Heartbeat para Firebase
      await this.firebase.sendHeartbeat(this.baseId, {
        timestamp: Date.now(),
        bikes_connected: this.connectedBikes.size,
        heap: Math.floor(Math.random() * 50000) + 200000, // Simula heap
        uptime: this.uptime
      });
      
    }, 30000);
  }

  log(message) {
    console.log(chalk.blue(`[Central ${this.baseId}] ${message}`));
  }

  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

module.exports = { Central };