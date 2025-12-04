#!/usr/bin/env node

/**
 * Script para sincronizar mudanças do firmware C++ para o emulador Node.js
 * 
 * Uso: node sync-firmware.js
 */

const fs = require('fs');
const path = require('path');
const chalk = require('chalk');

class FirmwareSync {
  constructor() {
    this.firmwarePath = path.join(__dirname, '../firmware');
    this.emulatorPath = path.join(__dirname, 'src');
  }

  async sync() {
    console.log(chalk.blue('🔄 Sincronizando firmware -> emulador\n'));

    // Analisa mudanças no firmware
    await this.analyzeCentralFirmware();
    await this.analyzeBikeFirmware();
    
    console.log(chalk.green('\n✅ Sincronização completa'));
    console.log(chalk.yellow('⚠️  Revise as mudanças e ajuste o emulador manualmente'));
  }

  async analyzeCentralFirmware() {
    console.log(chalk.cyan('🏢 Analisando firmware da Central...'));
    
    const mainFile = path.join(this.firmwarePath, 'central/src/main.cpp');
    
    if (!fs.existsSync(mainFile)) {
      console.log(chalk.red('❌ main.cpp não encontrado'));
      return;
    }

    const content = fs.readFileSync(mainFile, 'utf8');
    
    // Extrai configurações importantes
    const configs = this.extractConfigs(content);
    const ledPatterns = this.extractLEDPatterns(content);
    const bleLogic = this.extractBLELogic(content);
    
    console.log('📋 Configurações encontradas:');
    configs.forEach(config => console.log(`  - ${config}`));
    
    console.log('💡 Padrões de LED:');
    ledPatterns.forEach(pattern => console.log(`  - ${pattern}`));
    
    console.log('📡 Lógica BLE:');
    bleLogic.forEach(logic => console.log(`  - ${logic}`));
  }

  async analyzeBikeFirmware() {
    console.log(chalk.cyan('\n🚲 Analisando firmware da Bike...'));
    
    const mainFile = path.join(this.firmwarePath, 'bike/src/main.cpp');
    
    if (!fs.existsSync(mainFile)) {
      console.log(chalk.red('❌ main.cpp não encontrado'));
      return;
    }

    const content = fs.readFileSync(mainFile, 'utf8');
    
    const wifiLogic = this.extractWiFiLogic(content);
    const batteryLogic = this.extractBatteryLogic(content);
    
    console.log('📡 Lógica WiFi:');
    wifiLogic.forEach(logic => console.log(`  - ${logic}`));
    
    console.log('🔋 Lógica de Bateria:');
    batteryLogic.forEach(logic => console.log(`  - ${logic}`));
  }

  extractConfigs(content) {
    const configs = [];
    
    // Procura por #define
    const defines = content.match(/#define\\s+\\w+\\s+.+/g) || [];
    defines.forEach(define => {
      if (define.includes('INTERVAL') || define.includes('TIMEOUT') || define.includes('PIN')) {
        configs.push(define.replace('#define ', ''));
      }
    });
    
    return configs;
  }

  extractLEDPatterns(content) {
    const patterns = [];
    
    // Procura por padrões de LED
    const ledMatches = content.match(/led.*\\(.*\\)/gi) || [];
    ledMatches.forEach(match => {
      patterns.push(match);
    });
    
    return patterns;
  }

  extractBLELogic(content) {
    const logic = [];
    
    // Procura por funções BLE
    const bleMatches = content.match(/ble\\w+.*\\(/gi) || [];
    bleMatches.forEach(match => {
      logic.push(match);
    });
    
    return logic;
  }

  extractWiFiLogic(content) {
    const logic = [];
    
    // Procura por funções WiFi
    const wifiMatches = content.match(/wifi\\w+.*\\(/gi) || [];
    wifiMatches.forEach(match => {
      logic.push(match);
    });
    
    return logic;
  }

  extractBatteryLogic(content) {
    const logic = [];
    
    // Procura por lógica de bateria
    const batteryMatches = content.match(/battery.*|voltage.*/gi) || [];
    batteryMatches.forEach(match => {
      logic.push(match);
    });
    
    return logic;
  }
}

if (require.main === module) {
  const sync = new FirmwareSync();
  sync.sync().catch(console.error);
}