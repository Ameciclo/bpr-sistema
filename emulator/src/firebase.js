const chalk = require('chalk');
const fs = require('fs');
const path = require('path');

class MockFirebase {
  constructor() {
    // Carrega configurações reais
    const configPath = path.join(__dirname, '../../scripts/central_configs.json');
    let realConfigs = {};
    
    try {
      if (fs.existsSync(configPath)) {
        realConfigs = JSON.parse(fs.readFileSync(configPath, 'utf8'));
        this.log('✅ Configurações reais carregadas');
      }
    } catch (error) {
      this.log('⚠️  Usando configurações padrão');
    }

    this.data = {
      config: {
        version: 3,
        wifi_scan_interval_sec: 25,
        wifi_scan_interval_low_batt_sec: 60,
        deep_sleep_after_sec: 300,
        ble_ping_interval_sec: 5,
        min_battery_voltage: 3.45,
        update_timestamp: Date.now()
      },
      hub_configs: realConfigs,
      hubs: {},
      bicis: {},
      wifi_scans: {},
      rides: {},
      alerts: {}
    };
  }

  async getHubConfig(hubId) {
    this.log(`📥 Buscando config para hub ${hubId}`);
    await this.sleep(500);
    
    const config = this.data.hub_configs[hubId];
    if (!config) {
      this.log(`⚠️ Config não encontrada para ${hubId}, usando padrão`);
      return {
        hub_id: hubId,
        sync_interval_sec: 300,
        wifi_timeout_sec: 30,
        led_pin: 8,
        firebase_batch_size: 8000
      };
    }
    
    this.log(`✅ Config encontrada para ${hubId}`);
    return config;
  }

  async updateBiciStatus(biciId, status, hubId = null) {
    this.log(`📝 Atualizando status da bici ${biciId}: ${status}`);
    
    if (!this.data.bicis[biciId]) {
      this.data.bicis[biciId] = { uid: biciId };
    }
    
    this.data.bicis[biciId].status = status;
    this.data.bicis[biciId].hub_id = hubId;
    this.data.bicis[biciId].last_update = Date.now();
    
    await this.sleep(200);
  }

  async updateBiciData(biciId, data) {
    this.log(`📝 Atualizando dados da bici ${biciId}`);
    
    if (!this.data.bicis[biciId]) {
      this.data.bicis[biciId] = { uid: biciId };
    }
    
    Object.assign(this.data.bicis[biciId], data);
    await this.sleep(200);
  }

  async updateHubStatus(hubId, data) {
    this.log(`📝 Atualizando status do hub ${hubId}`);
    
    if (!this.data.hubs[hubId]) {
      this.data.hubs[hubId] = { hub_id: hubId };
    }
    
    Object.assign(this.data.hubs[hubId], data);
    await this.sleep(200);
  }

  async uploadWiFiScan(biciId, scanData) {
    const networks = scanData.scans || scanData.networks || [];
    this.log(`📡 Upload scan WiFi da bici ${biciId}: ${networks.length} redes`);
    
    if (!this.data.wifi_scans[biciId]) {
      this.data.wifi_scans[biciId] = {};
    }
    
    this.data.wifi_scans[biciId][scanData.timestamp || Date.now()] = networks;
    await this.sleep(300);
  }

  async uploadRide(biciId, rideData) {
    this.log(`🚲 Upload viagem da bici ${biciId}: ${rideData.km.toFixed(1)}km`);
    
    if (!this.data.rides[biciId]) {
      this.data.rides[biciId] = {};
    }
    
    const rideId = `ride_${Date.now()}`;
    this.data.rides[biciId][rideId] = rideData;
    
    await this.sleep(500);
  }

  async createAlert(type, biciId, data) {
    this.log(`🚨 Criando alerta ${type} para bici ${biciId}`);
    
    if (!this.data.alerts[type]) {
      this.data.alerts[type] = {};
    }
    
    this.data.alerts[type][biciId] = {
      timestamp: Date.now(),
      ...data
    };
    
    await this.sleep(200);
  }

  async sendHeartbeat(hubId, heartbeatData) {
    this.log(`💓 Heartbeat do hub ${hubId}: ${heartbeatData.bicis_connected} bicis`);
    
    if (!this.data.hubs[hubId]) {
      this.data.hubs[hubId] = { hub_id: hubId };
    }
    
    this.data.hubs[hubId].last_heartbeat = heartbeatData;
    await this.sleep(100);
  }

  async setHeartbeat(hubId, heartbeat) {
    return this.sendHeartbeat(hubId, heartbeat);
  }

  async uploadBatch(batchData) {
    this.log(`📦 Upload batch: ${batchData.length} items`);
    
    for (const item of batchData) {
      switch (item.type) {
        case 'bici_connected':
          await this.updateBiciData(item.bici_id, item.data);
          break;
        case 'wifi_data':
          await this.uploadWiFiScan(item.bici_id, item.data);
          break;
        case 'low_battery':
          await this.createAlert('battery_low', item.bici_id, { voltage: item.voltage });
          break;
      }
    }
    
    await this.sleep(500);
  }

  // Método para visualizar dados
  showData() {
    console.log(chalk.yellow('\n📊 Estado atual do Firebase Mock:\n'));
    
    console.log(chalk.blue('🏢 Hubs:'));
    Object.entries(this.data.hubs).forEach(([id, hub]) => {
      console.log(`  ${id}: ${hub.last_heartbeat?.bicis_connected || 0} bicis conectadas`);
    });
    
    console.log(chalk.cyan('\n🚲 Bicis:'));
    Object.entries(this.data.bicis).forEach(([id, bici]) => {
      console.log(`  ${id}: ${bici.status} (${bici.battery_voltage?.toFixed(2)}V)`);
    });
    
    console.log(chalk.red('\n🚨 Alertas:'));
    Object.entries(this.data.alerts).forEach(([type, alerts]) => {
      console.log(`  ${type}: ${Object.keys(alerts).length} alertas`);
    });
    
    console.log();
  }

  log(message) {
    console.log(chalk.gray(`[Firebase] ${message}`));
  }

  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

module.exports = { MockFirebase };