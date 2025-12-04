const chalk = require('chalk');

class Bike {
  constructor(bikeId, firebase) {
    this.id = bikeId;
    this.firebase = firebase;
    this.battery = 3.8; // Voltage inicial
    this.status = 'booting';
    this.position = { lat: -8.064, lng: -34.882 };
    this.connectedToCentral = false;
    this.wifiScans = [];
    this.rideData = null;
  }

  async boot() {
    this.log('🔄 Inicializando bike...');
    
    // Simula inicialização
    await this.sleep(1000);
    
    this.log('📡 Procurando central BLE...');
    await this.sleep(2000);
    
    this.status = 'ready';
    this.log('✅ Bike pronta');
  }

  async connectToCentral(central) {
    this.log(`🔗 Conectando à central ${central.baseId}...`);
    
    // Simula handshake BLE
    await this.sleep(1500);
    
    this.connectedToCentral = true;
    this.status = 'connected';
    
    await central.onBikeConnected(this.id);
    await this.firebase.updateBikeStatus(this.id, 'connected', central.baseId);
    
    this.log(`✅ Conectada à central ${central.baseId}`);
  }

  async sendHeartbeat() {
    if (!this.connectedToCentral) return;
    
    const heartbeat = {
      bike_id: this.id,
      battery_voltage: this.battery,
      timestamp: Date.now(),
      status: this.status
    };
    
    this.log(`💓 Enviando heartbeat: ${this.battery}V`);
    await this.firebase.updateBikeData(this.id, heartbeat);
  }

  async leaveBase() {
    this.log('🚪 Saindo da base...');
    this.connectedToCentral = false;
    this.status = 'active';
    
    // Inicia uma nova viagem
    this.rideData = {
      start_ts: Date.now(),
      route: [{ ...this.position }],
      km: 0
    };
    
    await this.sleep(1000);
    this.log('🚲 Fora da base - Viagem iniciada');
  }

  async performWiFiScan() {
    this.log('📡 Realizando scan WiFi...');
    
    // Simula scan de redes WiFi fictícias
    const mockNetworks = [
      { ssid: 'NET_5G_' + Math.floor(Math.random() * 1000), bssid: this.generateMAC(), rssi: -60 - Math.floor(Math.random() * 30) },
      { ssid: 'CLARO_WIFI', bssid: this.generateMAC(), rssi: -70 - Math.floor(Math.random() * 20) },
      { ssid: 'VIVO_FIBRA', bssid: this.generateMAC(), rssi: -80 - Math.floor(Math.random() * 15) }
    ];
    
    this.wifiScans.push({
      timestamp: Date.now(),
      networks: mockNetworks,
      position: { ...this.position }
    });
    
    this.log(`📶 Scan completo: ${mockNetworks.length} redes encontradas`);
    
    // Simula consumo de bateria
    this.battery -= 0.01;
  }

  async move() {
    // Simula movimento (pequeno deslocamento)
    this.position.lat += (Math.random() - 0.5) * 0.001;
    this.position.lng += (Math.random() - 0.5) * 0.001;
    
    if (this.rideData) {
      this.rideData.route.push({ ...this.position });
      this.rideData.km += 0.1; // Simula 100m por movimento
    }
    
    this.log(`📍 Nova posição: ${this.position.lat.toFixed(6)}, ${this.position.lng.toFixed(6)}`);
    
    // Simula consumo de bateria
    this.battery -= 0.005;
  }

  async returnToBase() {
    this.log('🏠 Retornando à base...');
    
    // Finaliza viagem
    if (this.rideData) {
      this.rideData.end_ts = Date.now();
      this.rideData.co2_saved_g = Math.floor(this.rideData.km * 150); // 150g CO2/km
      
      this.log(`📊 Viagem finalizada: ${this.rideData.km.toFixed(1)}km, ${this.rideData.co2_saved_g}g CO2 economizado`);
    }
    
    // Volta para posição da base
    this.position = { lat: -8.064, lng: -34.882 };
    this.status = 'returning';
    
    await this.sleep(2000);
    this.log('🏁 De volta à base');
  }

  async syncData() {
    this.log('🔄 Sincronizando dados...');
    
    // Upload dos scans WiFi
    for (const scan of this.wifiScans) {
      await this.firebase.uploadWiFiScan(this.id, scan);
    }
    
    // Upload da viagem
    if (this.rideData) {
      await this.firebase.uploadRide(this.id, this.rideData);
    }
    
    // Atualiza dados da bike
    await this.firebase.updateBikeData(this.id, {
      battery_voltage: this.battery,
      last_position: this.position,
      last_sync: Date.now()
    });
    
    // Limpa dados locais
    this.wifiScans = [];
    this.rideData = null;
    
    this.log('✅ Sincronização completa');
  }

  async sendBatteryAlert() {
    this.log(`🔋 Enviando alerta de bateria baixa: ${this.battery}V`);
    await this.firebase.createAlert('battery_low', this.id, { voltage: this.battery });
  }

  generateMAC() {
    return Array.from({length: 6}, () => 
      Math.floor(Math.random() * 256).toString(16).padStart(2, '0')
    ).join(':').toUpperCase();
  }

  log(message) {
    console.log(chalk.cyan(`[Bike ${this.id}] ${message}`));
  }

  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

module.exports = { Bike };