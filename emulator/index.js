#!/usr/bin/env node

const { BPREmulator } = require('./src/emulator');
const inquirer = require('inquirer');
const chalk = require('chalk');

async function main() {
  console.log(chalk.blue.bold('\n🚲 BPR Sistema Emulador\n'));
  
  const { scenario } = await inquirer.prompt([{
    type: 'list',
    name: 'scenario',
    message: 'Escolha o cenário para emular:',
    choices: [
      { name: '🏢 Central inicializando e configurando', value: 'central_boot' },
      { name: '🚲 Bike conectando na central', value: 'bike_connect' },
      { name: '🔄 Fluxo completo: Central + Bike + Viagem', value: 'full_flow' },
      { name: '🔋 Teste de bateria baixa', value: 'low_battery' },
      { name: '📡 Múltiplas bikes simultâneas', value: 'multi_bikes' },
      { name: '🧪 Cenário customizado', value: 'custom' }
    ]
  }]);

  const emulator = new BPREmulator();
  await emulator.run(scenario);
}

if (require.main === module) {
  main().catch(console.error);
}