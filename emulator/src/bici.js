const chalk = require('chalk');

class Bici {
  constructor(id, firebase) {
    this.id = id;
    this.firebase = firebase;
    this.state = 'BOOT';
    this.config = {};
    this.wifiBuffer = [];
    this.battery = 3.8;
    this.connectedHub = null;
    this.lastScan = 0;
    this.uptime = 0;
  }

  log(message) {
    console.log(chalk.cyan(`[BICI ${this.id}] ${message}`));
  }

  async boot() {
    this.log('🚀 Booting bici...');
    await this.sleep(300);
    
    // Simula inicialização
    this.log('💾 Initializing LittleFS...');
    this.log('📡 WiFi mode: STA');
    this.log('🔵 BLE device initialized');
    
    // Gera ID único se necessário
    if (!this.id.startsWith('bpr-')) {
      this.id = `bpr-${Math.random().toString(36).substr(2, 6)}`;
      this.log(`🆔 Generated unique ID: ${this.id}`);
    }
    
    // Carrega configuração
    await this.loadConfig();
    
    // Carrega buffer salvo
    await this.loadBuffer();
    
    this.log('✅ Boot complete');
    this.uptime = Date.now();
  }

  async loadConfig() {
    this.log('📂 Loading config from LittleFS...');
    
    // Simula configuração padrão
    this.config = {
      bike_id: this.id,
      version: 1,
      dev_mode: true,
      wifi: {
        scan_interval_sec: 300,
        scan_interval_low_batt_sec: 900,
        scan_timeout_ms: 5000,
        max_networks: 20,
        rssi_threshold: -90
      },
      ble: {
        base_name: 'BPR Hub Station',
        scan_time_sec: 5,
        connection_timeout_ms: 10000
      },
      power: {
        radio_coordination_delay_ms: 300,
        light_sleep_duration_ms: 1000,
        deep_sleep_duration_sec: 3600,
        max_time_without_base_sec: 7200
      },
      battery: {
        critical_voltage: 3.2,
        low_voltage: 3.45,
        full_voltage: 4.2
      },
      timing: {
        status_report_interval_ms: 30000,
        emergency_button_hold_ms: 3000
      },
      buffers: {
        max_wifi_records: 100
      }
    };
    
    this.log(`✅ Config loaded: ${this.id} v${this.config.version}`);
    this.log(`📡 WiFi: ${this.config.wifi.scan_interval_sec}s interval, ${this.config.wifi.scan_timeout_ms}ms timeout`);
    this.log(`🔵 BLE: ${this.config.ble.scan_time_sec}s scan, base='${this.config.ble.base_name}'`);
    this.log(`🔋 Battery: ${this.config.battery.critical_voltage}V critical, ${this.config.battery.low_voltage}V low`);
  }

  async saveConfig() {
    this.log('💾 Saving config to LittleFS...');
    // Simula salvamento
    await this.sleep(100);
    this.log('✅ Config saved successfully');
  }

  async loadBuffer() {
    this.log('📂 Loading WiFi buffer...');
    // Simula carregamento de buffer salvo
    this.wifiBuffer = [];
    this.log(`📂 Buffer loaded: ${this.wifiBuffer.length} records`);
  }

  async saveBuffer() {
    this.log('💾 Saving buffer to LittleFS...');
    // Simula salvamento
    await this.sleep(100);
    this.log('💾 Buffer saved');
  }

  async enterState(newState) {
    const oldState = this.state;
    this.state = newState;
    this.log(`🔄 State: ${oldState} → ${newState}`);
    
    switch (newState) {
      case 'CONFIG_REQUEST':
        await this.handleConfigRequest();
        break;
      case 'SCANNING':
        await this.handleScanning();
        break;
      case 'AT_BASE':
        await this.handleAtBase();
        break;
      case 'SLEEP':
        await this.handleSleep();
        break;
    }
  }

  async handleConfigRequest() {
    this.log('🔍 CONFIG REQUEST - Searching for BLE hub to get configuration...');
    
    // Simula busca por hub
    await this.sleep(1000);
    
    if (Math.random() > 0.3) { // 70% chance de encontrar
      this.log('✅ Hub found! Requesting configuration...');
      
      if (await this.requestConfigFromHub()) {
        this.log('✅ Configuration received and saved!');
        await this.enterState('AT_BASE');
      } else {
        this.log('❌ Failed to get configuration from hub');
        await this.sleep(2000);
      }
    } else {
      this.log('❌ No BLE hub found, retrying...');
      await this.sleep(2000);
    }
  }

  async handleScanning() {
    this.log('📡 SCANNING - Collecting WiFi data...');
    
    const now = Date.now();
    const interval = this.battery < this.config.battery.low_voltage ? 
      this.config.wifi.scan_interval_low_batt_sec : 
      this.config.wifi.scan_interval_sec;
    
    if (now - this.lastScan > interval * 1000) {
      await this.performWiFiScan();
      this.lastScan = now;
      
      // Radio coordination delay
      this.log(`⏱️ Radio coordination delay: ${this.config.power.radio_coordination_delay_ms}ms`);
      await this.sleep(this.config.power.radio_coordination_delay_ms);
      
      // Procura hub a cada 10s
      if (Math.random() < 0.1) { // 10% chance
        if (await this.scanForHub()) {
          await this.enterState('AT_BASE');
          return;
        }
      }
    }
    
    // Verifica bateria
    if (this.battery < this.config.battery.critical_voltage && !this.config.dev_mode) {
      await this.enterState('SLEEP');
    } else if (this.battery < this.config.battery.low_voltage) {
      this.log('🔋 Low battery mode activated');
    }
    
    await this.sleep(1000);
  }

  async handleAtBase() {
    if (!this.connectedHub) {
      await this.enterState('SCANNING');
      return;
    }
    
    this.log('🏠 AT_BASE - Syncing data with hub');
    
    // Envia status
    await this.sendStatus();
    
    // Envia dados WiFi se disponível
    if (this.wifiBuffer.length > 0) {
      await this.sendWiFiData();
      this.wifiBuffer = []; // Limpa buffer
    }
    
    await this.sleep(5000);
    
    // Simula perda de conexão
    if (Math.random() < 0.2) { // 20% chance
      this.log('📡 BLE connection lost');
      this.connectedHub = null;
      await this.enterState('SCANNING');
    }
  }

  async handleSleep() {
    this.log('💤 Deep sleep for 1 hour');
    
    // Salva buffer
    await this.saveBuffer();
    
    // Simula deep sleep
    this.log('💤 Entering deep sleep...');
    await this.sleep(2000);
    
    // Simula wake-up
    this.log('⏰ Wake-up timer triggered');
    await this.enterState('BOOT');
  }

  async performWiFiScan() {
    this.log(`📡 Starting WiFi scan (timeout: ${this.config.wifi.scan_timeout_ms}ms, max: ${this.config.wifi.max_networks} networks)...`);
    
    // Simula scan WiFi
    const networks = Math.floor(Math.random() * 15) + 5; // 5-20 redes
    let saved = 0;
    
    for (let i = 0; i < networks && this.wifiBuffer.length < this.config.buffers.max_wifi_records; i++) {
      const rssi = -40 - Math.floor(Math.random() * 50); // -40 a -90 dBm
      
      if (rssi > this.config.wifi.rssi_threshold) {
        const record = {
          timestamp: Math.floor(Date.now() / 1000),
          bssid: this.generateRandomBSSID(),
          rssi: rssi
        };
        this.wifiBuffer.push(record);
        saved++;
      }
    }
    
    this.log(`📶 Found ${networks} networks, saved ${saved} (buffer: ${this.wifiBuffer.length}/${this.config.buffers.max_wifi_records})`);
  }

  async scanForHub() {
    this.log(`🔍 Scanning for BLE hub '${this.config.ble.base_name}*' (timeout: ${this.config.ble.scan_time_sec}s)...`);
    
    // Simula scan BLE
    await this.sleep(this.config.ble.scan_time_sec * 200); // Simula tempo de scan
    
    if (Math.random() > 0.7) { // 30% chance de encontrar
      this.log('🔍 Found hub: BPR Hub Station');
      return await this.connectToHub();
    }
    
    this.log('❌ No BLE hub found');
    return false;
  }

  async connectToHub(hub = null) {
    this.log(`🔗 Attempting BLE connection (timeout: ${this.config.ble.connection_timeout_ms}ms)...`);
    
    // Simula conexão
    await this.sleep(1000);
    
    if (Math.random() > 0.2) { // 80% chance de sucesso
      this.connectedHub = hub || { id: 'hub01' };
      this.log('✅ BLE connection established');
      
      if (hub) {
        await hub.onBiciConnected(this.id, {
          battery: this.battery,
          records: this.wifiBuffer.length,
          timestamp: Date.now()
        });
      }
      
      return true;
    }
    
    this.log('❌ BLE connection failed');
    return false;
  }

  async requestConfigFromHub(hub = null) {
    if (!this.connectedHub && !hub) {
      this.log('❌ No BLE connection to request config');
      return false;
    }
    
    this.log('📡 Requesting configuration from hub...');
    
    // Simula solicitação de config
    const configRequest = {
      type: 'config_request',
      bike_id: this.id
    };
    
    this.log(`📤 Config request: ${JSON.stringify(configRequest)}`);
    
    // Simula resposta do hub
    if (hub) {
      const newConfig = await hub.provideBiciConfig(this.id);
      this.config = { ...this.config, ...newConfig };
      this.log(`📥 Config received: ${newConfig.bike_name}`);
    } else {
      this.log('⚠️ Simulation: Using default config');
    }
    
    // Salva configuração
    await this.saveConfig();
    return true;
  }

  async sendStatus() {
    if (!this.connectedHub) return;
    
    const status = {
      bike_id: this.id,
      battery: this.battery,
      records: this.wifiBuffer.length,
      timestamp: Math.floor(Date.now() / 1000),
      heap: 174248,
      uptime: Math.floor((Date.now() - this.uptime) / 1000)
    };
    
    this.log(`📤 Status: battery=${this.battery}V, records=${this.wifiBuffer.length}`);
  }

  async sendWiFiData() {
    if (!this.connectedHub || this.wifiBuffer.length === 0) return;
    
    const wifiData = {
      scans: this.wifiBuffer.map(record => ({
        ts: record.timestamp,
        bssid: record.bssid,
        rssi: record.rssi
      }))
    };
    
    this.log(`📡 WiFi data: ${this.wifiBuffer.length} records`);
    
    if (this.connectedHub.receiveWiFiData) {
      await this.connectedHub.receiveWiFiData(this.id, wifiData);
    }
  }

  async sendBatteryAlert() {
    this.log(`🔋 Battery alert: ${this.battery}V (critical: ${this.config.battery.critical_voltage}V)`);
    
    if (this.connectedHub && this.connectedHub.onLowBattery) {
      await this.connectedHub.onLowBattery(this.id, this.battery);
    }
  }

  async move() {
    // Simula movimento (descarga de bateria)
    this.battery -= 0.01;
    if (this.battery < 3.0) this.battery = 4.2; // Reset para teste
    
    this.log(`🚶 Moving... battery: ${this.battery.toFixed(2)}V`);
  }

  async syncData() {
    this.log('🔄 Syncing all data with hub...');
    await this.sendStatus();
    await this.sendWiFiData();
    this.log('✅ Data sync complete');
  }

  generateRandomBSSID() {
    const hex = '0123456789ABCDEF';
    let bssid = '';
    for (let i = 0; i < 6; i++) {
      if (i > 0) bssid += ':';
      bssid += hex[Math.floor(Math.random() * 16)];
      bssid += hex[Math.floor(Math.random() * 16)];
    }
    return bssid;
  }

  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

module.exports = { Bici };