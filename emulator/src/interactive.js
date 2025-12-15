const chalk = require('chalk');
const inquirer = require('inquirer');
const { Hub } = require('./hub');
const { Bici } = require('./bici');
const { MockFirebase } = require('./firebase');

class InteractiveEmulator {
  constructor() {
    this.firebase = new MockFirebase();
    this.hub = null;
    this.bicis = [];
  }

  async run() {
    console.log(chalk.blue.bold('\n🎮 Modo Interativo BPR\n'));
    
    while (true) {
      const { action } = await inquirer.prompt([{
        type: 'list',
        name: 'action',
        message: 'O que fazer?',
        choices: [
          { name: '🏢 Iniciar Hub', value: 'start_hub' },
          { name: '🚲 Adicionar Bici', value: 'add_bici' },
          { name: '🔄 Simular Viagem', value: 'simulate_trip' },
          { name: '🔋 Testar Bateria Baixa', value: 'low_battery' },
          { name: '📊 Ver Estado Firebase', value: 'show_data' },
          { name: '❌ Sair', value: 'exit' }
        ]
      }]);

      switch (action) {
        case 'start_hub':
          await this.startHub();
          break;
        case 'add_bici':
          await this.addBici();
          break;
        case 'simulate_trip':
          await this.simulateTrip();
          break;
        case 'low_battery':
          await this.testLowBattery();
          break;
        case 'show_data':
          this.firebase.showData();
          break;
        case 'exit':
          console.log(chalk.yellow('👋 Tchau!'));
          return;
      }
    }
  }

  async startHub() {
    if (this.hub) {
      console.log(chalk.yellow('⚠️  Hub já está rodando'));
      return;
    }

    this.hub = new Hub('hub01', this.firebase);
    await this.hub.boot();
    await this.hub.enterState('BLE_ONLY');
  }

  async addBici() {
    if (!this.hub) {
      console.log(chalk.red('❌ Inicie o hub primeiro'));
      return;
    }

    const biciId = `bpr-${String(this.bicis.length + 1).padStart(6, '0')}`;
    const bici = new Bici(biciId, this.firebase);
    
    await bici.boot();
    await bici.connectToHub(this.hub);
    await bici.enterState('AT_BASE');
    
    this.bicis.push(bici);
    console.log(chalk.green(`✅ Bici ${biciId} adicionada (${this.bicis.length} total)`));
  }

  async simulateTrip() {
    if (this.bicis.length === 0) {
      console.log(chalk.red('❌ Adicione bicis primeiro'));
      return;
    }

    const { biciIndex } = await inquirer.prompt([{
      type: 'list',
      name: 'biciIndex',
      message: 'Qual bici vai viajar?',
      choices: this.bicis.map((bici, i) => ({ name: bici.id, value: i }))
    }]);

    const bici = this.bicis[biciIndex];
    
    await bici.enterState('SCANNING');
    await this.hub.onBiciLeft(bici.id);
    
    // Simula viagem
    for (let i = 0; i < 3; i++) {
      await bici.performWiFiScan();
      await bici.move();
      await this.sleep(1000);
    }
    
    await bici.connectToHub(this.hub);
    await bici.enterState('AT_BASE');
    await bici.syncData();
  }

  async testLowBattery() {
    if (this.bicis.length === 0) {
      console.log(chalk.red('❌ Adicione bicis primeiro'));
      return;
    }

    const bici = this.bicis[0];
    bici.battery = 3.2;
    
    await bici.sendBatteryAlert();
    await this.hub.onLowBattery(bici.id, bici.battery);
  }

  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

module.exports = { InteractiveEmulator };