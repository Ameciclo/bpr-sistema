#include "web_server.h"
#include "config.h"
#include "wifi_scanner.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <Arduino.h>

void startConfigMode() {
  configMode = true;
  
  // Sempre criar AP no modo configuração
  WiFi.mode(WIFI_AP);
  String apName = "Bike-" + String(config.bikeId);
  bool apStarted = WiFi.softAP(apName.c_str(), config.apPassword);
  
  Serial.println("=== MODO CONFIGURAÇÃO ATIVO ===");
  if (apStarted) {
    Serial.println("📶 AP Status: ATIVO");
    Serial.println("📶 WiFi: " + apName);
    Serial.println("🔐 Senha: " + String(config.apPassword));
    Serial.println("🌐 Acesse: http://192.168.4.1");
    Serial.println("📱 IP do AP: " + WiFi.softAPIP().toString());
  } else {
    Serial.println("❌ ERRO: Falha ao criar AP!");
  }
  Serial.println("💬 Menu serial disponível - digite 'm'");
  Serial.println("================================");

  // Configurar rotas do servidor web
  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/wifi", handleWifi);
  server.on("/dados", handleDados);
  server.on("/download", handleDownload);
  server.on("/test", handleTest);
  server.on("/exit", handleExit);
  
  // Iniciar servidor web
  server.begin();
  Serial.println("🌐 Servidor web iniciado na porta 80");
}

void handleRoot() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;font-size:18px}.btn{display:block;padding:20px;margin:15px 0;background:#007cba;color:white;text-decoration:none;text-align:center;border-radius:5px;font-size:18px}</style></head>";
  html += "<body><h1>Bike " + String(config.bikeId) + " - Controle</h1>";
  html += "<div style='background:#e8f5e8;border:1px solid #4caf50;padding:15px;margin:15px 0;border-radius:5px'>";
  html += "🔋 <strong>Bateria: " + String(getBatteryLevel(), 1) + "% (" + String(getBatteryVoltage(), 3) + "V)</strong> | ";
  html += "⏱️ Uptime: " + String(millis()/1000) + "s | ";
  html += "💾 Dados: " + String(dataCount) + " arquivos";
  html += "</div>";
  html += "<a href='/config' class='btn'>1) Configurações</a>";
  html += "<a href='/wifi' class='btn'>2) Ver WiFi Detectados</a>";
  html += "<a href='/dados' class='btn'>3) Ver Dados Gravados</a>";
  html += "<a href='/download' class='btn' style='background:#28a745'>4) Baixar Todos os Dados</a>";
  html += "<a href='/test' class='btn' style='background:#ffc107;color:black'>5) Testar Firebase</a>";
  html += "<a href='/exit' class='btn' style='background:#dc3545'>6) Sair do Modo AP</a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleConfig() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;font-size:18px}input,button,select{padding:15px;font-size:16px;margin:10px 0;width:100%;box-sizing:border-box}h3{margin-top:30px}</style>";
  html += "<script>";
  html += "function updateTimes(){";
  html += "var mode=document.getElementsByName('mode')[0].value;";
  html += "var active=document.getElementsByName('active')[0];";
  html += "var inactive=document.getElementsByName('inactive')[0];";
  html += "if(mode=='mega_economico'){active.value=30000;inactive.value=120000;}";
  html += "else if(mode=='economico'){active.value=15000;inactive.value=60000;}";
  html += "else if(mode=='normal'){active.value=5000;inactive.value=30000;}";
  html += "else if(mode=='intensivo'){active.value=2000;inactive.value=15000;}";
  html += "else if(mode=='extremo'){active.value=1000;inactive.value=5000;}";
  html += "}";
  html += "function checkCustom(){";
  html += "var mode=document.getElementsByName('mode')[0];";
  html += "var active=parseInt(document.getElementsByName('active')[0].value);";
  html += "var inactive=parseInt(document.getElementsByName('inactive')[0].value);";
  html += "var isCustom=true;";
  html += "if((active==30000&&inactive==120000)||(active==15000&&inactive==60000)||(active==5000&&inactive==30000)||(active==2000&&inactive==15000)||(active==1000&&inactive==5000))isCustom=false;";
  html += "if(isCustom){mode.value='personalizado';}";
  html += "}";
  float currentVoltage = getBatteryVoltage();
  float rawVoltage = currentVoltage / config.batteryCalibration;
  html += "function updateBatteryPreview(){";
  html += "var newCoef=parseFloat(document.getElementsByName('battery_cal')[0].value)||1.0;";
  html += "var rawVoltage=" + String(rawVoltage, 3) + ";";
  html += "var newVoltage=rawVoltage*newCoef;";
  html += "var color=newVoltage>=4.0?'green':(newVoltage>=3.5?'orange':'red');";
  html += "document.getElementById('battery_preview').innerHTML='<span style=\"color:'+color+';font-weight:bold\">Preview: '+newVoltage.toFixed(3)+'V</span>';";
  html += "}";
  html += "</script></head>";
  html += "<body><h1>Configurações - Bike " + String(config.bikeId) + "</h1>";
  html += "<a href='/' style='display:block;padding:10px;background:#666;color:white;text-decoration:none;text-align:center;margin:10px 0'>Voltar</a>";
  html += "<form method='post' action='/save'>";
  html += "ID da Bicicleta: <input name='bike' value='" + String(config.bikeId) + "'><br>";
  html += "<h3>Modo de Coleta:</h3>";
  html += "<select name='mode' onchange='updateTimes()' style='padding:15px;font-size:16px;width:100%;margin:10px 0'>";
  html += "<option value='mega_economico'";
  if (strcmp(config.collectMode, "mega_economico") == 0) html += " selected";
  html += ">Mega Econômico (30s/2min)</option>";
  html += "<option value='economico'";
  if (strcmp(config.collectMode, "economico") == 0) html += " selected";
  html += ">Econômico (15s/1min)</option>";
  html += "<option value='normal'";
  if (strcmp(config.collectMode, "normal") == 0) html += " selected";
  html += ">Normal (5s/30s)</option>";
  html += "<option value='intensivo'";
  if (strcmp(config.collectMode, "intensivo") == 0) html += " selected";
  html += ">Intensivo (2s/15s)</option>";
  html += "<option value='extremo'";
  if (strcmp(config.collectMode, "extremo") == 0) html += " selected";
  html += ">Extremo (1s/5s)</option>";
  html += "<option value='personalizado'";
  if (strcmp(config.collectMode, "personalizado") == 0) html += " selected";
  html += ">Personalizado</option>";
  html += "</select><br>";
  html += "<small>Tempos: (movimento/base). Modos mais intensivos consomem mais bateria.</small><br><br>";
  html += "<h3>Ajuste Manual (opcional):</h3>";
  html += "Tempo Scan Ativo (ms): <input name='active' value='" + String(config.scanTimeActive) + "' placeholder='Auto' oninput='checkCustom()'><br>";
  html += "Tempo Scan Inativo (ms): <input name='inactive' value='" + String(config.scanTimeInactive) + "' placeholder='Auto' oninput='checkCustom()'><br>";
  html += "<h3>Base 1:</h3>SSID: <input name='ssid1' value='" + String(config.baseSSID1) + "'><br>";
  html += "Senha: <input name='pass1' value='" + String(config.basePassword1) + "'><br>";
  html += "<h3>Base 2:</h3>SSID: <input name='ssid2' value='" + String(config.baseSSID2) + "'><br>";
  html += "Senha: <input name='pass2' value='" + String(config.basePassword2) + "'><br>";
  html += "<h3>Base 3:</h3>SSID: <input name='ssid3' value='" + String(config.baseSSID3) + "'><br>";
  html += "Senha: <input name='pass3' value='" + String(config.basePassword3) + "'><br>";
  html += "<h3>Detecção de Proximidade:</h3>RSSI Mínimo: <input name='rssi' value='" + String(config.baseProximityRssi) + "' placeholder='-80'><br>";
  html += "<small>Valores: -70 (próximo), -80 (médio), -90 (longe)</small><br>";
  html += "<h3>Firebase:</h3>URL: <input name='firebase_url' value='" + String(config.firebaseUrl) + "' placeholder='https://projeto-default-rtdb.firebaseio.com'><br>";
  html += "Key: <input name='firebase_key' value='" + String(config.firebaseKey) + "' placeholder='AIzaSy...'><br>";
  html += "<h3>Limpeza:</h3>";
  html += "<label><input type='checkbox' name='cleanup'" + String(config.cleanupEnabled ? " checked" : "") + "> Apagar dados após upload</label><br>";
  html += "Máximo uploads histórico: <input name='max_uploads' value='" + String(config.maxUploadsHistory) + "' placeholder='10'><br>";
  html += "<h3>Intervalos:</h3>";
  html += "Sincronização NTP (horas): <input name='ntp_interval' value='" + String(config.ntpSyncIntervalHours) + "' placeholder='6'><br>";
  html += "Upload status (minutos): <input name='status_interval' value='" + String(config.statusUploadIntervalMinutes) + "' placeholder='30'><br>";
  html += "<h3>Segurança:</h3>";
  html += "Senha do AP: <input name='ap_password' value='" + String(config.apPassword) + "' placeholder='12345678' minlength='8'><br>";
  html += "<small>Mínimo 8 caracteres para segurança</small><br>";
  html += "<h3>Calibração da Bateria:</h3>";
  html += "<div style='background:#f0f8ff;border:1px solid #4169e1;padding:10px;margin:10px 0;border-radius:5px'>";
  html += "<strong>Atual:</strong> " + String(currentVoltage, 3) + "V (coef: " + String(config.batteryCalibration, 3) + ")<br>";
  html += "<strong>Tensão bruta:</strong> " + String(rawVoltage, 3) + "V<br>";
  html += "<span id='battery_preview' style='color:#4169e1;font-weight:bold'>Preview: " + String(currentVoltage, 3) + "V</span>";
  html += "</div>";
  html += "Fator calibração: <input name='battery_cal' value='" + String(config.batteryCalibration, 3) + "' placeholder='1.000' step='0.001' oninput='updateBatteryPreview()'><br>";
  html += "<small>Ajuste o fator até o preview igualar a leitura do multímetro</small><br>";
  html += "<button type='submit'>Salvar</button></form></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  server.arg("bike").toCharArray(config.bikeId, 10);
  
  // Aplicar modo de coleta
  String mode = server.arg("mode");
  if (mode.length() > 0) {
    mode.toCharArray(config.collectMode, 20);
    if (strcmp(config.collectMode, "personalizado") != 0) {
      applyCollectMode(config.collectMode);
    }
  }
  
  // Override manual se especificado (sempre aplicar para modo personalizado)
  if (server.arg("active").length() > 0 && server.arg("active").toInt() > 0) {
    config.scanTimeActive = server.arg("active").toInt();
  }
  if (server.arg("inactive").length() > 0 && server.arg("inactive").toInt() > 0) {
    config.scanTimeInactive = server.arg("inactive").toInt();
  }
  server.arg("ssid1").toCharArray(config.baseSSID1, 32);
  server.arg("pass1").toCharArray(config.basePassword1, 32);
  server.arg("ssid2").toCharArray(config.baseSSID2, 32);
  server.arg("pass2").toCharArray(config.basePassword2, 32);
  server.arg("ssid3").toCharArray(config.baseSSID3, 32);
  server.arg("pass3").toCharArray(config.basePassword3, 32);
  
  String rssiValue = server.arg("rssi");
  if (rssiValue.length() > 0) {
    config.baseProximityRssi = rssiValue.toInt();
  }
  
  server.arg("firebase_url").toCharArray(config.firebaseUrl, 128);
  server.arg("firebase_key").toCharArray(config.firebaseKey, 64);
  config.cleanupEnabled = server.hasArg("cleanup");
  
  String maxUploads = server.arg("max_uploads");
  if (maxUploads.length() > 0) {
    config.maxUploadsHistory = maxUploads.toInt();
  }
  
  String ntpInterval = server.arg("ntp_interval");
  if (ntpInterval.length() > 0) {
    config.ntpSyncIntervalHours = ntpInterval.toInt();
  }
  
  String statusInterval = server.arg("status_interval");
  if (statusInterval.length() > 0) {
    config.statusUploadIntervalMinutes = statusInterval.toInt();
  }
  
  String batteryCal = server.arg("battery_cal");
  if (batteryCal.length() > 0) {
    config.batteryCalibration = batteryCal.toFloat();
    if (config.batteryCalibration < 0.5 || config.batteryCalibration > 2.0) {
      config.batteryCalibration = 1.0; // Valor seguro se fora do range
    }
  }
  
  String apPassword = server.arg("ap_password");
  if (apPassword.length() >= 8) {
    apPassword.toCharArray(config.apPassword, 32);
  }

  saveConfig();
  server.send(200, "text/html", "<html><body><h1>Salvo! Reiniciando...</h1></body></html>");
  delay(2000);
  ESP.restart();
}

void handleWifi() {
  scanWiFiNetworks();
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;font-size:16px}table{width:100%;border-collapse:collapse}th,td{border:1px solid #ddd;padding:12px;text-align:left}th{background:#f2f2f2}.strong{color:green}.weak{color:red}</style>";
  html += "<script>setTimeout(function(){location.reload()},5000)</script></head>";
  html += "<body><h1>WiFi Detectados (Bike " + String(config.bikeId) + ")</h1>";
  html += "<p>Atualizando a cada 5 segundos...</p>";
  html += "<table><tr><th>SSID</th><th>RSSI</th><th>Canal</th></tr>";
  for (int i = 0; i < networkCount; i++) {
    String cssClass = networks[i].rssi > -60 ? "strong" : (networks[i].rssi < -80 ? "weak" : "");
    html += "<tr class='" + cssClass + "'><td>" + String(networks[i].ssid) + "</td><td>" + String(networks[i].rssi) + " dBm</td><td>" + String(networks[i].channel) + "</td></tr>";
  }
  html += "</table><br><a href='/'>Voltar</a></body></html>";
  server.send(200, "text/html", html);
}

void handleDados() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;font-size:16px}.arquivo{border:1px solid #ddd;padding:10px;margin:10px 0;background:#f9f9f9}.debug{background:#fff3cd;border:1px solid #ffeaa7;padding:10px;margin:10px 0}</style></head>";
  html += "<body><h1>Dados Gravados - Bike " + String(config.bikeId) + "</h1>";
  html += "<div style='background:#e8f5e8;border:1px solid #4caf50;padding:10px;margin:10px 0;border-radius:5px'>";
  html += "🔋 Bateria: " + String(getBatteryLevel(), 1) + "% (" + String(getBatteryVoltage(), 3) + "V) | ";
  html += "💾 " + String(dataCount) + " arquivos salvos";
  html += "</div>";
  html += "<a href='/' style='display:block;padding:10px;background:#666;color:white;text-decoration:none;text-align:center;margin:10px 0;width:100px'>Voltar</a>";
  html += "<a href='/download' style='display:block;padding:10px;background:#28a745;color:white;text-decoration:none;text-align:center;margin:10px 0;width:200px'>Baixar Todos os Dados</a>";
  
  // Debug info
  html += "<div class='debug'>";
  html += "<strong>DEBUG:</strong><br>";
  html += "configMode: " + String(configMode ? "true" : "false") + "<br>";
  html += "dataCount: " + String(dataCount) + "<br>";
  html += "networkCount: " + String(networkCount) + "<br>";
  html += "uptime: " + String(millis()/1000) + "s<br>";
  html += "LittleFS usado: " + String(LittleFS.usedBytes()) + "/" + String(LittleFS.totalBytes()) + " bytes<br>";
  html += "</div>";
  
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  int count = 0;
  while (file && count < 10) {
    String fileName = file.name();
    if (fileName.startsWith("/scan_") || fileName.startsWith("scan_")) {
      count++;
      html += "<div class='arquivo'>";
      html += "<strong>" + fileName + "</strong> (" + String(file.size()) + " bytes)<br>";
      file.close();
      File dataFile = LittleFS.open(fileName.c_str(), "r");
      if (dataFile) {
        String content = dataFile.readString();
        dataFile.close();
        html += "<code>" + content + "</code>";
      }
      html += "</div>";
    } else {
      file.close();
    }
    file = root.openNextFile();
  }
  root.close();
  
  if (count >= 10) {
    html += "<p><em>Mostrando apenas os primeiros 10 arquivos...</em></p>";
  }
  
  if (count == 0) {
    html += "<p><strong>⚠️ Nenhum arquivo de dados encontrado!</strong></p>";
    html += "<p>Possíveis causas:</p>";
    html += "<ul>";
    html += "<li>Sistema em modo configuração (não coleta dados)</li>";
    html += "<li>Nenhum scan WiFi realizado ainda</li>";
    html += "<li>Erro no sistema de arquivos</li>";
    html += "</ul>";
    
    // Listar TODOS os arquivos para debug
    html += "<h3>Todos os arquivos no sistema:</h3>";
    File debugRoot = LittleFS.open("/");
    File debugFile = debugRoot.openNextFile();
    int totalFiles = 0;
    while (debugFile) {
      html += "📄 " + String(debugFile.name()) + " (" + String(debugFile.size()) + " bytes)<br>";
      totalFiles++;
      debugFile.close();
      debugFile = debugRoot.openNextFile();
    }
    debugRoot.close();
    html += "<strong>Total de arquivos: " + String(totalFiles) + "</strong>";
  }
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleDownload() {
  String json = "{";
  json += "\"bikeId\":\"" + String(config.bikeId) + "\",";
  json += "\"exportTime\":" + String(millis()) + ",";
  json += "\"totalFiles\":" + String(dataCount) + ",";
  json += "\"scans\":[";
  
  File root = LittleFS.open("/");
  bool first = true;
  int processedFiles = 0;
  File file = root.openNextFile();
  
  while (file) {
    String fileName = file.name();
    if (fileName.startsWith("/scan_") || fileName.startsWith("scan_")) {
      file.close();
      File dataFile = LittleFS.open(fileName.c_str(), "r");
      if (dataFile) {
        String content = dataFile.readString();
        dataFile.close();
        
        // Só adicionar se o conteúdo não estiver vazio
        if (content.length() > 0) {
          if (!first) json += ",";
          json += content;
          first = false;
          processedFiles++;
        }
      }
    } else {
      file.close();
    }
    file = root.openNextFile();
  }
  root.close();
  
  json += "],";
  json += "\"processedFiles\":" + String(processedFiles);
  json += "}";
  
  Serial.printf("💾 Download: %d arquivos processados, JSON: %d bytes\n", processedFiles, json.length());
  
  server.sendHeader("Content-Disposition", "attachment; filename=bike_" + String(config.bikeId) + "_data.json");
  server.send(200, "application/json", json);
}

void handleTest() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;font-size:16px}.result{border:1px solid #ddd;padding:15px;margin:15px 0;background:#f9f9f9}.success{border-color:#28a745;background:#d4edda}.error{border-color:#dc3545;background:#f8d7da}</style></head>";
  html += "<body><h1>Teste Firebase - Bike " + String(config.bikeId) + "</h1>";
  html += "<a href='/' style='display:block;padding:10px;background:#666;color:white;text-decoration:none;text-align:center;margin:10px 0;width:100px'>Voltar</a>";
  
  html += "<h3>Configurações:</h3>";
  html += "<div class='result'>";
  html += "<strong>URL:</strong> " + String(config.firebaseUrl) + "<br>";
  html += "<strong>Key:</strong> " + String(config.firebaseKey).substring(0, 15) + "...<br>";
  html += "</div>";
  
  html += "<h3>Teste de Conectividade:</h3>";
  html += "<div class='result'>";
  
  if (strlen(config.firebaseUrl) == 0) {
    html += "<span class='error'>❌ Firebase não configurado</span>";
  } else {
    html += "<span class='success'>✅ Firebase configurado</span><br>";
    
    // Testar conexão WiFi
    if (WiFi.status() == WL_CONNECTED) {
      html += "<span class='success'>✅ WiFi conectado: " + WiFi.localIP().toString() + "</span><br>";
      
      // Fazer teste real do Firebase
      html += "<strong>Testando conexão Firebase...</strong><br>";
      
      WiFiClientSecure client;
      client.setInsecure();
      
      String url = String(config.firebaseUrl);
      url.replace("https://", "");
      url.replace("http://", "");
      int slashIndex = url.indexOf('/');
      String host = url.substring(0, slashIndex);
      
      if (client.connect(host.c_str(), 443)) {
        html += "<span class='success'>✅ Conexão Firebase OK</span><br>";
        
        // Teste de escrita
        String testPath = "/bikes/" + String(config.bikeId) + "/test.json";
        String testData = "{\"bike\":\"" + String(config.bikeId) + "\",\"time\":" + String(millis()) + ",\"test\":true}";
        
        client.print("PUT " + testPath + " HTTP/1.1\r\n");
        client.print("Host: " + host + "\r\n");
        client.print("Content-Type: application/json\r\n");
        client.print("Content-Length: " + String(testData.length()) + "\r\n");
        client.print("Connection: close\r\n\r\n");
        client.print(testData);
        
        delay(1000);
        String response = "";
        int timeout = 0;
        while (client.available() && timeout < 50) {
          response += client.readString();
          delay(10);
          timeout++;
        }
        
        if (response.indexOf("200 OK") > 0) {
          html += "<span class='success'>✅ Teste de upload OK!</span><br>";
        } else {
          html += "<span class='error'>❌ Erro no upload: " + response.substring(0, 100) + "</span><br>";
        }
        
        client.stop();
      } else {
        html += "<span class='error'>❌ Falha ao conectar no Firebase</span><br>";
      }
    } else {
      html += "<span class='error'>❌ WiFi desconectado</span><br>";
    }
  }
  
  html += "</div>";
  
  // Mostrar histórico de uploads
  html += "<h3>Histórico de Uploads:</h3>";
  html += "<div class='result'>";
  
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  int uploadCount = 0;
  while (file && uploadCount < 10) {
    String fileName = file.name();
    if (fileName.startsWith("/upload_")) {
      uploadCount++;
      file.close();
      File uploadFile = LittleFS.open(fileName.c_str(), "r");
      if (uploadFile) {
        String content = uploadFile.readString();
        uploadFile.close();
        html += "<strong>" + fileName + ":</strong> " + content + "<br>";
      }
    } else {
      file.close();
    }
    file = root.openNextFile();
  }
  root.close();
  
  if (uploadCount == 0) {
    html += "<em>Nenhum upload registrado ainda</em>";
  }
  
  html += "</div>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleExit() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;font-size:18px;text-align:center}</style>";
  html += "<script>setTimeout(function(){window.close()},3000)</script></head>";
  html += "<body><h1>Saindo do Modo AP...</h1>";
  html += "<p>O sistema voltará ao modo normal em alguns segundos.</p>";
  html += "<p>Esta janela fechará automaticamente.</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
  
  // Sinalizar para sair do modo configuração
  configMode = false;
}