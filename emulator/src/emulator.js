const chalk = require('chalk');
const { Hub } = require('./hub');
const { Bici } = require('./bici');
const { MockFirebase } = require('./firebase');

class BPREmulator {
  constructor() {
    this.firebase = new MockFirebase();
    this.hub = null;
    this.bicis = [];
    this.running = false;
  }

  async run(scenario) {
    this.running = true;
    console.log(chalk.yellow(`\n▶️  Iniciando cenário: ${scenario}\n`));

    switch (scenario) {
      case 'hub_boot':
        await this.hubBootScenario();
        break;
      case 'bici_connect':
        await this.biciConnectScenario();
        break;
      case 'full_flow':
        await this.fullFlowScenario();
        break;
      case 'low_battery':
        await this.lowBatteryScenario();
        break;
      case 'multi_bicis':
        await this.multiBicisScenario();
        break;
      case 'config_request':
        await this.configRequestScenario();
        break;
      default:
        console.log(chalk.red('Cenário não implementado'));
    }
  }

  async hubBootScenario() {
    console.log(chalk.blue('🏢 Simulando inicialização do Hub...\n'));
    
    this.hub = new Hub('hub01', this.firebase);
    await this.hub.boot();
    await this.hub.enterState('CONFIG_AP');
    await this.hub.enterState('BLE_ONLY');
    
    console.log(chalk.green('✅ Hub pronto e aguardando bicis\n'));
    await this.sleep(2000);
  }

  async biciConnectScenario() {
    await this.hubBootScenario();
    
    console.log(chalk.cyan('🚲 Simulando bici se conectando...\n'));
    
    const bici = new Bici('bpr-abc123', this.firebase);
    this.bicis.push(bici);
    
    await bici.boot();
    await bici.enterState('CONFIG_REQUEST');
    await bici.connectToHub(this.hub);
    await bici.enterState('AT_BASE');
    
    console.log(chalk.green('✅ Bici conectada com sucesso\n'));
    await this.sleep(2000);
  }

  async fullFlowScenario() {
    await this.biciConnectScenario();
    
    console.log(chalk.magenta('🔄 Simulando viagem completa...\n'));
    
    const bici = this.bicis[0];
    
    // Bici sai da base
    await bici.enterState('SCANNING');
    await this.hub.onBiciLeft(bici.id);
    
    // Simula movimento e scans WiFi
    for (let i = 0; i < 3; i++) {
      await bici.performWiFiScan();
      await bici.move();
      await this.sleep(1000);
    }
    
    // Bici volta para base
    await bici.connectToHub(this.hub);
    await bici.enterState('AT_BASE');
    await bici.syncData();
    
    console.log(chalk.green('✅ Viagem completa simulada\n'));
  }

  async lowBatteryScenario() {
    await this.biciConnectScenario();
    
    console.log(chalk.red('🔋 Simulando bateria baixa...\n'));
    
    const bici = this.bicis[0];
    bici.battery = 3.2; // Bateria baixa
    
    await bici.sendBatteryAlert();
    await this.hub.onLowBattery(bici.id, bici.battery);
    
    console.log(chalk.yellow('⚠️  Alerta de bateria baixa processado\n'));
  }

  async multiBicisScenario() {
    await this.hubBootScenario();
    
    console.log(chalk.cyan('📡 Simulando múltiplas bicis...\n'));
    
    // Cria 3 bicis
    for (let i = 1; i <= 3; i++) {
      const bici = new Bici(`bpr-${i.toString().padStart(6, '0')}`, this.firebase);
      this.bicis.push(bici);
      
      await bici.boot();
      await bici.connectToHub(this.hub);
      await this.sleep(500);
    }
    
    console.log(chalk.green(`✅ ${this.bicis.length} bicis conectadas\n`));
    
    // Simula atividade simultânea
    const promises = this.bicis.map(async (bici, index) => {
      await this.sleep(index * 1000); // Stagger
      await bici.enterState('SCANNING');
      await bici.performWiFiScan();
      await bici.enterState('AT_BASE');
    });
    
    await Promise.all(promises);
    console.log(chalk.green('✅ Atividade simultânea concluída\n'));
  }

  async configRequestScenario() {
    await this.hubBootScenario();
    
    console.log(chalk.yellow('⚙️ Simulando solicitação de configuração...\n'));
    
    const bici = new Bici('bpr-new001', this.firebase);
    this.bicis.push(bici);
    
    await bici.boot();
    await bici.enterState('CONFIG_REQUEST');
    await bici.requestConfigFromHub(this.hub);
    await bici.enterState('AT_BASE');
    
    console.log(chalk.green('✅ Configuração recebida e aplicada\n'));
  }

  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

module.exports = { BPREmulator };