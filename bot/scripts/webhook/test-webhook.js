const axios = require('axios');

const WEBHOOK_URL = 'https://us-central1-botaprarodar-routes.cloudfunctions.net/telegramBot';

const testMessage = {
  update_id: 123456789,
  message: {
    message_id: 1,
    from: {
      id: 123456789,
      is_bot: false,
      first_name: "Test",
      username: "test"
    },
    chat: {
      id: 123456789,
      first_name: "Test",
      username: "test",
      type: "private"
    },
    date: Math.floor(Date.now() / 1000),
    text: "/start"
  }
};

console.log('🔄 Testando webhook...');
console.log('URL:', WEBHOOK_URL);

axios.post(WEBHOOK_URL, testMessage)
  .then(response => {
    console.log('✅ Resposta:', response.status, response.data);
  })
  .catch(error => {
    console.log('❌ Erro:', error.response?.status, error.response?.data || error.message);
  });