const chalk = require('chalk');

class Hub {
  constructor(id, firebase) {
    this.id = id;
    this.firebase = firebase;
    this.state = 'BOOT';
    this.config = {};
    this.connectedBicis = new Map();
    this.buffer = [];
    this.ledState = 'off';
    this.syncTimer = null;
    this.heartbeatTimer = null;
  }

  log(message) {
    console.log(chalk.blue(`[HUB ${this.id}] ${message}`));
  }

  async boot() {
    this.log('🚀 Booting hub...');
    await this.sleep(500);
    
    // Simula inicialização do hardware
    this.log('💾 Initializing LittleFS...');
    this.log('🔧 Hardware self-check...');
    this.log('💡 LED controller ready');
    
    // Carrega configuração
    await this.loadConfig();
    
    this.log('✅ Boot complete');
  }

  async loadConfig() {
    this.log('📂 Loading configuration...');
    
    // Simula carregamento do Firebase
    this.config = await this.firebase.getHubConfig(this.id) || {
      hub_id: this.id,
      sync_interval_sec: 300,
      wifi_timeout_sec: 30,
      led_pin: 8,
      firebase_batch_size: 8000,
      ntp_server: 'pool.ntp.org',
      timezone_offset: -10800,
      led: {
        boot_ms: 100,
        ble_ready_ms: 2000,
        wifi_sync_ms: 500,
        error_ms: 50
      }
    };
    
    this.log(`⚙️ Config loaded: sync=${this.config.sync_interval_sec}s`);
  }

  async enterState(newState) {
    const oldState = this.state;
    this.state = newState;
    this.log(`🔄 State: ${oldState} → ${newState}`);
    
    switch (newState) {
      case 'CONFIG_AP':
        await this.handleConfigAP();
        break;
      case 'BLE_ONLY':
        await this.handleBLEOnly();
        break;
      case 'WIFI_SYNC':
        await this.handleWiFiSync();
        break;
      case 'SHUTDOWN':
        await this.handleShutdown();
        break;
    }
  }

  async handleConfigAP() {
    this.log('📱 Entering CONFIG_AP mode...');
    this.setLED('config');
    
    // Simula AP para configuração
    this.log('📡 Starting AP: BPR_Hub_Config');
    this.log('🌐 Web interface: http://192.168.4.1');
    
    await this.sleep(2000);
    
    // Simula configuração completa
    this.log('✅ Configuration received');
    await this.enterState('BLE_ONLY');
  }

  async handleBLEOnly() {
    this.log('🔵 Entering BLE_ONLY mode...');
    this.setLED('ble_ready');
    
    // Inicia servidor BLE
    this.log('🔵 Starting BLE server: "BPR Hub Station"');
    this.log('📡 Service UUID: 12345678-1234-1234-1234-123456789abc');
    this.log('📝 Data characteristic: 87654321-4321-4321-4321-cba987654321');
    this.log('⚙️ Config characteristic: 11111111-2222-3333-4444-555555555555');
    
    // Inicia timers
    this.startSyncTimer();
    this.startHeartbeatTimer();
    
    this.log('✅ BLE server ready, waiting for bicis...');
  }

  async handleWiFiSync() {
    this.log('📡 Entering WIFI_SYNC mode...');
    this.setLED('wifi_sync');
    
    // Desabilita BLE
    this.log('🔵 Disabling BLE...');
    
    // Conecta WiFi
    this.log('📶 Connecting to WiFi...');
    await this.sleep(1000);
    this.log('✅ WiFi connected');
    
    // Sincroniza NTP
    this.log('⏰ Syncing NTP...');
    await this.sleep(500);
    
    // Download configurações
    this.log('📥 Downloading configurations...');
    await this.loadConfig();
    
    // Upload buffer
    if (this.buffer.length > 0) {
      this.log(`📤 Uploading ${this.buffer.length} buffered records...`);
      await this.firebase.uploadBatch(this.buffer);
      this.buffer = [];
      this.log('✅ Buffer uploaded');
    }
    
    // Envia heartbeat
    await this.sendHeartbeat();
    
    this.log('📡 WiFi sync complete');
    await this.enterState('BLE_ONLY');
  }

  async handleShutdown() {
    this.log('💤 Entering SHUTDOWN mode...');
    this.setLED('off');
    
    // Para timers
    if (this.syncTimer) clearInterval(this.syncTimer);
    if (this.heartbeatTimer) clearInterval(this.heartbeatTimer);
    
    // Salva buffer
    this.log('💾 Saving buffer to LittleFS...');
    
    this.log('💤 Deep sleep for power saving...');
    await this.sleep(2000);
    
    // Simula wake-up
    this.log('⏰ Wake-up timer triggered');
    await this.enterState('BLE_ONLY');
  }

  setLED(pattern) {
    this.ledState = pattern;
    const patterns = {
      'boot': '💡 LED: Fast blink (100ms)',
      'config': '💡 LED: Medium blink (200ms)', 
      'ble_ready': '💡 LED: Slow blink (2s)',
      'wifi_sync': '💡 LED: Medium blink (500ms)',
      'error': '💡 LED: Very fast blink (50ms)',
      'off': '💡 LED: Off'
    };
    
    if (patterns[pattern]) {
      this.log(patterns[pattern]);
    }
  }

  startSyncTimer() {
    this.syncTimer = setInterval(() => {
      if (this.state === 'BLE_ONLY' && (this.buffer.length > 50 || Math.random() < 0.1)) {
        this.log('⏰ Sync timer triggered');
        this.enterState('WIFI_SYNC');
      }
    }, this.config.sync_interval_sec * 1000);
  }

  startHeartbeatTimer() {
    this.heartbeatTimer = setInterval(() => {
      if (this.state === 'BLE_ONLY') {
        this.sendHeartbeat();
      }
    }, 60000); // 1 minuto
  }

  async sendHeartbeat() {
    const heartbeat = {
      timestamp: Date.now(),
      bicis_connected: this.connectedBicis.size,
      heap: 174248,
      uptime: Math.floor(Date.now() / 1000),
      state: this.state
    };
    
    await this.firebase.setHeartbeat(this.id, heartbeat);
    this.log(`💓 Heartbeat sent: ${this.connectedBicis.size} bicis connected`);
  }

  async onBiciConnected(biciId, biciData) {
    this.connectedBicis.set(biciId, biciData);
    this.log(`🚲 Bici connected: ${biciId}`);
    
    // LED: 3 piscadas rápidas
    this.log('💡 LED: 3 quick blinks (bici arrived)');
    
    // Adiciona ao buffer
    this.buffer.push({
      type: 'bici_connected',
      bici_id: biciId,
      timestamp: Date.now(),
      data: biciData
    });
  }

  async onBiciDisconnected(biciId) {
    this.connectedBicis.delete(biciId);
    this.log(`🚲 Bici disconnected: ${biciId}`);
    
    // LED: 1 piscada longa
    this.log('💡 LED: 1 long blink (bici left)');
  }

  async onBiciLeft(biciId) {
    this.log(`🚀 Bici ${biciId} left the base`);
    await this.onBiciDisconnected(biciId);
  }

  async onLowBattery(biciId, voltage) {
    this.log(`🔋 Low battery alert: ${biciId} = ${voltage}V`);
    
    const alert = {
      type: 'low_battery',
      bici_id: biciId,
      voltage: voltage,
      timestamp: Date.now()
    };
    
    this.buffer.push(alert);
    await this.firebase.addAlert('battery_low', biciId, Date.now());
  }

  async receiveWiFiData(biciId, wifiData) {
    this.log(`📡 WiFi data from ${biciId}: ${wifiData.scans?.length || 0} scans`);
    
    this.buffer.push({
      type: 'wifi_data',
      bici_id: biciId,
      timestamp: Date.now(),
      data: wifiData
    });
    
    // Trigger sync se buffer muito cheio
    if (this.buffer.length > this.config.firebase_batch_size / 100) {
      this.log('📦 Buffer getting full, triggering sync...');
      await this.enterState('WIFI_SYNC');
    }
  }

  async provideBiciConfig(biciId) {
    this.log(`⚙️ Providing config to ${biciId}`);
    
    const biciConfig = {
      bike_id: biciId,
      bike_name: `Bike ${biciId}`,
      version: 1,
      dev_mode: false,
      wifi: {
        scan_interval_sec: 300,
        scan_timeout_ms: 5000
      },
      ble: {
        base_name: 'BPR Hub Station',
        scan_time_sec: 5
      },
      power: {
        deep_sleep_duration_sec: 3600
      },
      battery: {
        critical_voltage: 3.2,
        low_voltage: 3.45
      }
    };
    
    return biciConfig;
  }

  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

module.exports = { Hub };