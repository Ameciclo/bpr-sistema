// Função corrigida para converter WiFi em coordenadas
async function convertWifiToCoordinates(networks) {
    try {
        // Detectar formato dos dados
        const isArrayFormat = Array.isArray(networks[0]);
        
        const wifiAccessPoints = networks.map(net => {
            if (isArrayFormat) {
                // Formato array: [ssid, bssid, rssi, channel]
                return {
                    macAddress: net[1],
                    signalStrength: net[2], 
                    channel: net[3]
                };
            } else {
                // Formato objeto: {ssid, bssid, rssi, channel}
                return {
                    macAddress: net.bssid,
                    signalStrength: net.rssi,
                    channel: net.channel
                };
            }
        });

        const response = await axios.post(
            `https://www.googleapis.com/geolocation/v1/geolocate?key=${GOOGLE_API_KEY}`, 
            { wifiAccessPoints }
        );
        
        if (response.status === 200 && response.data.location) {
            return {
                lat: response.data.location.lat,
                lng: response.data.location.lng,
                accuracy: response.data.accuracy
            };
        }
    } catch (error) {
        console.log('Erro na geolocalização:', error.response?.data || error.message);
    }
    return null;
}

console.log('✅ Função corrigida para suportar ambos os formatos de dados');
console.log('📝 Copie esta função para substituir a original no código do bot');