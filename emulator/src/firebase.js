const chalk = require('chalk');

class MockFirebase {
  constructor() {
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
      central_configs: {
        base01: {
          base_id: 'base01',
          sync_interval_sec: 300,
          wifi_timeout_sec: 30,
          led_pin: 8,
          ntp_server: 'pool.ntp.org',
          timezone_offset: -10800,
          firebase_batch_size: 8000,
          led: {
            boot_ms: 100,
            ble_ready_ms: 2000,
            wifi_sync_ms: 500,
            bike_arrived_ms: 200,
            bike_left_ms: 1000,
            error_ms: 50
          }
        }
      },
      bases: {},
      bikes: {},
      wifi_scans: {},
      rides: {},
      alerts: {}
    };
  }

  async getCentralConfig(baseId) {
    this.log(`📥 Buscando config para central ${baseId}`);
    await this.sleep(500);
    
    const config = this.data.central_configs[baseId];
    if (!config) {
      throw new Error(`Config não encontrada para ${baseId}`);
    }
    
    this.log(`✅ Config encontrada para ${baseId}`);
    return config;
  }

  async updateBikeStatus(bikeId, status, baseId = null) {
    this.log(`📝 Atualizando status da bike ${bikeId}: ${status}`);
    
    if (!this.data.bikes[bikeId]) {
      this.data.bikes[bikeId] = { uid: bikeId };
    }
    
    this.data.bikes[bikeId].status = status;
    this.data.bikes[bikeId].base_id = baseId;
    this.data.bikes[bikeId].last_update = Date.now();
    
    await this.sleep(200);
  }

  async updateBikeData(bikeId, data) {
    this.log(`📝 Atualizando dados da bike ${bikeId}`);
    
    if (!this.data.bikes[bikeId]) {
      this.data.bikes[bikeId] = { uid: bikeId };
    }
    
    Object.assign(this.data.bikes[bikeId], data);
    await this.sleep(200);
  }

  async updateBaseStatus(baseId, data) {
    this.log(`📝 Atualizando status da base ${baseId}`);
    
    if (!this.data.bases[baseId]) {
      this.data.bases[baseId] = { base_id: baseId };
    }
    
    Object.assign(this.data.bases[baseId], data);
    await this.sleep(200);
  }

  async uploadWiFiScan(bikeId, scanData) {
    this.log(`📡 Upload scan WiFi da bike ${bikeId}: ${scanData.networks.length} redes`);
    
    if (!this.data.wifi_scans[bikeId]) {
      this.data.wifi_scans[bikeId] = {};
    }
    
    this.data.wifi_scans[bikeId][scanData.timestamp] = scanData.networks;
    await this.sleep(300);
  }

  async uploadRide(bikeId, rideData) {
    this.log(`🚲 Upload viagem da bike ${bikeId}: ${rideData.km.toFixed(1)}km`);
    
    if (!this.data.rides[bikeId]) {
      this.data.rides[bikeId] = {};
    }
    
    const rideId = `ride_${Date.now()}`;
    this.data.rides[bikeId][rideId] = rideData;
    
    await this.sleep(500);
  }

  async createAlert(type, bikeId, data) {
    this.log(`🚨 Criando alerta ${type} para bike ${bikeId}`);
    
    if (!this.data.alerts[type]) {
      this.data.alerts[type] = {};
    }
    
    this.data.alerts[type][bikeId] = {
      timestamp: Date.now(),
      ...data
    };
    
    await this.sleep(200);
  }

  async sendHeartbeat(baseId, heartbeatData) {
    this.log(`💓 Heartbeat da base ${baseId}: ${heartbeatData.bikes_connected} bikes`);
    
    if (!this.data.bases[baseId]) {
      this.data.bases[baseId] = { base_id: baseId };
    }
    
    this.data.bases[baseId].last_heartbeat = heartbeatData;
    await this.sleep(100);
  }

  // Método para visualizar dados
  showData() {
    console.log(chalk.yellow('\n📊 Estado atual do Firebase Mock:\n'));
    
    console.log(chalk.blue('🏢 Bases:'));
    Object.entries(this.data.bases).forEach(([id, base]) => {
      console.log(`  ${id}: ${base.last_heartbeat?.bikes_connected || 0} bikes conectadas`);
    });
    
    console.log(chalk.cyan('\n🚲 Bikes:'));
    Object.entries(this.data.bikes).forEach(([id, bike]) => {
      console.log(`  ${id}: ${bike.status} (${bike.battery_voltage?.toFixed(2)}V)`);
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