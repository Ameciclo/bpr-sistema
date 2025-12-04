const chalk = require('chalk');
const inquirer = require('inquirer');
const { Central } = require('./central');
const { Bike } = require('./bike');
const { MockFirebase } = require('./firebase');

class InteractiveEmulator {
  constructor() {
    this.firebase = new MockFirebase();
    this.central = null;
    this.bikes = [];
  }

  async run() {
    console.log(chalk.blue.bold('\n🎮 Modo Interativo BPR\n'));
    
    while (true) {
      const { action } = await inquirer.prompt([{
        type: 'list',
        name: 'action',
        message: 'O que fazer?',
        choices: [
          { name: '🏢 Iniciar Central', value: 'start_central' },
          { name: '🚲 Adicionar Bike', value: 'add_bike' },
          { name: '🔄 Simular Viagem', value: 'simulate_trip' },
          { name: '🔋 Testar Bateria Baixa', value: 'low_battery' },
          { name: '📊 Ver Estado Firebase', value: 'show_data' },
          { name: '❌ Sair', value: 'exit' }
        ]
      }]);

      switch (action) {
        case 'start_central':
          await this.startCentral();
          break;
        case 'add_bike':
          await this.addBike();
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

  async startCentral() {
    if (this.central) {
      console.log(chalk.yellow('⚠️  Central já está rodando'));
      return;
    }

    this.central = new Central('base01', this.firebase);
    await this.central.boot();
    await this.central.loadConfig();
    await this.central.startBLE();
  }

  async addBike() {
    if (!this.central) {
      console.log(chalk.red('❌ Inicie a central primeiro'));
      return;
    }

    const bikeId = `bike${String(this.bikes.length + 1).padStart(2, '0')}`;
    const bike = new Bike(bikeId, this.firebase);
    
    await bike.boot();
    await bike.connectToCentral(this.central);
    
    this.bikes.push(bike);
    console.log(chalk.green(`✅ Bike ${bikeId} adicionada (${this.bikes.length} total)`));
  }

  async simulateTrip() {
    if (this.bikes.length === 0) {
      console.log(chalk.red('❌ Adicione bikes primeiro'));
      return;
    }

    const { bikeIndex } = await inquirer.prompt([{
      type: 'list',
      name: 'bikeIndex',
      message: 'Qual bike vai viajar?',
      choices: this.bikes.map((bike, i) => ({ name: bike.id, value: i }))
    }]);

    const bike = this.bikes[bikeIndex];
    
    await bike.leaveBase();
    await this.central.onBikeLeft(bike.id);
    
    // Simula viagem
    for (let i = 0; i < 3; i++) {
      await bike.performWiFiScan();
      await bike.move();
      await this.sleep(1000);
    }
    
    await bike.returnToBase();
    await bike.connectToCentral(this.central);
    await bike.syncData();
  }

  async testLowBattery() {
    if (this.bikes.length === 0) {
      console.log(chalk.red('❌ Adicione bikes primeiro'));
      return;
    }

    const bike = this.bikes[0];
    bike.battery = 3.2;
    
    await bike.sendBatteryAlert();
    await this.central.onLowBattery(bike.id, bike.battery);
  }

  sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }
}

module.exports = { InteractiveEmulator };