#!/usr/bin/env node

const fs = require('fs');
const path = require('path');
const chalk = require('chalk');

class SmartFirmwareSync {
  constructor() {
    this.changes = [];
  }

  async sync() {
    console.log(chalk.blue('🔄 Análise inteligente do firmware\n'));

    const centralChanges = await this.analyzeCentral();
    const bikeChanges = await this.analyzeBike();
    
    this.generateSuggestions([...centralChanges, ...bikeChanges]);
  }

  async analyzeCentral() {
    const content = this.readFile('../firmware/central/src/main.cpp');
    if (!content) return [];

    return [
      ...this.findDefines(content, 'Central'),
      ...this.findFunctions(content, 'Central'),
      ...this.findLEDLogic(content)
    ];
  }

  findDefines(content, component) {
    const defines = content.match(/#define\s+(\w+)\s+(.+)/g) || [];
    return defines.map(define => {
      const [, name, value] = define.match(/#define\s+(\w+)\s+(.+)/);
      return {
        type: 'config',
        component,
        name,
        value: value.trim(),
        suggestion: `Atualizar ${component.toLowerCase()}.js: ${name.toLowerCase()}: ${value.trim()}`
      };
    });
  }

  findFunctions(content, component) {
    const functions = content.match(/void\s+(\w+)\s*\([^)]*\)/g) || [];
    return functions.map(func => {
      const [, name] = func.match(/void\s+(\w+)/);
      return {
        type: 'function',
        component,
        name,
        suggestion: `Implementar ${name}() em ${component.toLowerCase()}.js`
      };
    });
  }

  findLEDLogic(content) {
    const patterns = content.match(/led\w+\([^)]*\)/gi) || [];
    return patterns.map(pattern => ({
      type: 'led',
      component: 'Central',
      pattern,
      suggestion: `Adicionar padrão LED: ${pattern}`
    }));
  }

  generateSuggestions(changes) {
    console.log(chalk.yellow('📝 Sugestões de mudanças:\n'));
    
    changes.forEach((change, i) => {
      console.log(chalk.cyan(`${i + 1}. ${change.suggestion}`));
      
      if (change.type === 'config') {
        console.log(chalk.gray(`   // ${change.component}: ${change.name} = ${change.value}`));
      }
    });

    console.log(chalk.green('\n✅ Execute as mudanças manualmente no emulador'));
  }

  readFile(relativePath) {
    const fullPath = path.join(__dirname, relativePath);
    try {
      return fs.readFileSync(fullPath, 'utf8');
    } catch {
      console.log(chalk.red(`❌ Arquivo não encontrado: ${relativePath}`));
      return null;
    }
  }
}

if (require.main === module) {
  new SmartFirmwareSync().sync();
}