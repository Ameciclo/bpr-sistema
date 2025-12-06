const firebaseService = require('../config/firebase');

class SubscriptionManager {
  constructor() {
    this.subscriptions = new Map(); // Cache local
    this.init();
  }

  async init() {
    // Carregar assinaturas existentes
    await this.loadSubscriptions();
  }

  async loadSubscriptions() {
    try {
      const snapshot = await firebaseService.db.ref('subscriptions').once('value');
      const data = snapshot.val() || {};
      
      Object.entries(data).forEach(([userId, userSubs]) => {
        this.subscriptions.set(userId, userSubs);
      });
      
      console.log(`✅ ${this.subscriptions.size} assinaturas carregadas`);
    } catch (error) {
      console.error('Erro ao carregar assinaturas:', error);
    }
  }

  // Assinar bike específica
  async subscribeToBike(userId, bikeId) {
    const userSubs = this.subscriptions.get(userId) || { bikes: [], stations: [], system: false };
    
    if (!userSubs.bikes.includes(bikeId)) {
      userSubs.bikes.push(bikeId);
      this.subscriptions.set(userId, userSubs);
      await this.saveSubscription(userId, userSubs);
      return true;
    }
    return false;
  }

  // Assinar estação específica
  async subscribeToStation(userId, stationId) {
    const userSubs = this.subscriptions.get(userId) || { bikes: [], stations: [], system: false };
    
    if (!userSubs.stations.includes(stationId)) {
      userSubs.stations.push(stationId);
      this.subscriptions.set(userId, userSubs);
      await this.saveSubscription(userId, userSubs);
      return true;
    }
    return false;
  }

  // Assinar sistema inteiro
  async subscribeToSystem(userId) {
    const userSubs = this.subscriptions.get(userId) || { bikes: [], stations: [], system: false };
    
    if (!userSubs.system) {
      userSubs.system = true;
      this.subscriptions.set(userId, userSubs);
      await this.saveSubscription(userId, userSubs);
      return true;
    }
    return false;
  }

  // Desassinar
  async unsubscribe(userId, type, id = null) {
    const userSubs = this.subscriptions.get(userId);
    if (!userSubs) return false;

    let changed = false;
    
    switch (type) {
      case 'bike':
        const bikeIndex = userSubs.bikes.indexOf(id);
        if (bikeIndex > -1) {
          userSubs.bikes.splice(bikeIndex, 1);
          changed = true;
        }
        break;
      case 'station':
        const stationIndex = userSubs.stations.indexOf(id);
        if (stationIndex > -1) {
          userSubs.stations.splice(stationIndex, 1);
          changed = true;
        }
        break;
      case 'system':
        if (userSubs.system) {
          userSubs.system = false;
          changed = true;
        }
        break;
    }

    if (changed) {
      this.subscriptions.set(userId, userSubs);
      await this.saveSubscription(userId, userSubs);
    }
    
    return changed;
  }

  // Obter usuários interessados em uma bike
  getUsersForBike(bikeId) {
    const users = [];
    
    this.subscriptions.forEach((subs, userId) => {
      if (subs.bikes.includes(bikeId) || subs.system) {
        users.push(userId);
      }
    });
    
    return users;
  }

  // Obter usuários interessados em uma estação
  getUsersForStation(stationId) {
    const users = [];
    
    this.subscriptions.forEach((subs, userId) => {
      if (subs.stations.includes(stationId) || subs.system) {
        users.push(userId);
      }
    });
    
    return users;
  }

  // Obter usuários do sistema inteiro
  getSystemUsers() {
    const users = [];
    
    this.subscriptions.forEach((subs, userId) => {
      if (subs.system) {
        users.push(userId);
      }
    });
    
    return users;
  }

  // Obter assinaturas de um usuário
  getUserSubscriptions(userId) {
    return this.subscriptions.get(userId) || { bikes: [], stations: [], system: false };
  }

  // Salvar no Firebase
  async saveSubscription(userId, subscription) {
    try {
      await firebaseService.db.ref(`subscriptions/${userId}`).set(subscription);
    } catch (error) {
      console.error('Erro ao salvar assinatura:', error);
    }
  }
}

module.exports = new SubscriptionManager();