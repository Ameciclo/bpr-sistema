const chalk = require('chalk');
const { Central } = require('./central');
const { Bike } = require('./bike');
const { MockFirebase } = require('./firebase');

class BPREmulator {
  constructor() {
    this.firebase = new MockFirebase();
    this.central = null;
    this.bikes = [];
    this.running = false;
  }

  async run(scenario) {
    this.running = true;
    console.log(chalk.yellow(`\n▶️  Iniciando cenário: ${scenario}\n`));

    switch (scenario) {
      case 'central_boot':
        await this.centralBootScenario();
        break;
      case 'bike_connect':
        await this.bikeConnectScenario();
        break;
      case 'full_flow':
        await this.fullFlowScenario();
        break;
      case 'low_battery':
        await this.lowBatteryScenario();
        break;
      case 'multi_bikes':
        await this.multiBikesScenario();
        break;
      default:
        console.log(chalk.red('Cenário não implementado'));
    }
  }

  async centralBootScenario() {
    console.log(chalk.blue('🏢 Simulando inicialização da Central...\n'));
    
    this.central = new Central('base01', this.firebase);
    await this.central.boot();
    await this.central.loadConfig();
    await this.central.startBLE();
    
    console.log(chalk.green('✅ Central pronta e aguardando bikes\n'));
    await this.sleep(2000);
  }

  async bikeConnectScenario() {
    await this.centralBootScenario();
    
    console.log(chalk.cyan('🚲 Simulando bike se conectando...\n'));
    
    const bike = new Bike('bike07', this.firebase);
    this.bikes.push(bike);
    
    await bike.boot();
    await bike.connectToCentral(this.central);
    await bike.sendHeartbeat();
    
    console.log(chalk.green('✅ Bike conectada com sucesso\n'));
    await this.sleep(2000);
  }

  async fullFlowScenario() {
    await this.bikeConnectScenario();
    
    console.log(chalk.magenta('🔄 Simulando viagem completa...\n'));
    
    const bike = this.bikes[0];
    
    // Bike sai da base
    await bike.leaveBase();
    await this.central.onBikeLeft(bike.id);
    
    // Simula movimento e scans WiFi
    for (let i = 0; i < 3; i++) {
      await bike.performWiFiScan();
      await bike.move();
      await this.sleep(1000);
    }
    
    // Bike volta para base
    await bike.returnToBase();
    await bike.connectToCentral(this.central);
    await bike.syncData();
    
    console.log(chalk.green('✅ Viagem completa simulada\n'));
  }

  async lowBatteryScenario() {
    await this.bikeConnectScenario();
    
    console.log(chalk.red('🔋 Simulando bateria baixa...\n'));
    
    const bike = this.bikes[0];
    bike.battery = 3.2; // Bateria baixa
    
    await bike.sendBatteryAlert();
    await this.central.onLowBattery(bike.id, bike.battery);
    
    console.log(chalk.yellow('⚠️  Alerta de bateria baixa processado\n'));
  }

  async multiBikesScenario() {
    await this.centralBootScenario();
    
    console.log(chalk.cyan('📡 Simulando múltiplas bikes...\n'));
    
    // Cria 3 bikes
    for (let i = 1; i <= 3; i++) {
      const bike = new Bike(`bike0${i}`, this.firebase);
      this.bikes.push(bike);
      
      await bike.boot();
      await bike.connectToCentral(this.central);
      await this.sleep(500);
    }
    
    console.log(chalk.green(`✅ ${this.bikes.length} bikes conectadas\n`));
    
    // Simula atividade simultânea
    const promises = this.bikes.map(async (bike, index) => {
      await this.sleep(index * 1000); // Stagger
      await bike.leaveBase();
      await bike.performWiFiScan();
      await bike.returnToBase();
    });
    
    await Promise.all(promises);
    console.log(chalk.green('✅ Atividade simultânea concluída\n'));
  }

  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

module.exports = { BPREmulator };