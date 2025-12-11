#include "config_ap.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "constants.h"
#include "config_manager.h"
#include "led_controller.h"
#include "state_machine.h"

extern ConfigManager configManager;
extern LEDController ledController;
extern StateMachine stateMachine;

static WebServer server(80);
static uint32_t apStartTime = 0;

void ConfigAP::enter() {
    Serial.println("CONFIG_AP mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("AP: %s IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    setupWebServer();
    server.begin();
    apStartTime = millis();
    ledController.configPattern();
}

void ConfigAP::update() {
    server.handleClient();
    if (millis() - apStartTime > CONFIG_TIMEOUT_MS) {
        ESP.restart();
    }
}

void ConfigAP::exit() {
    server.stop();
    WiFi.softAPdisconnect(true);
}

void ConfigAP::setupWebServer() {
    server.on("/", HTTP_GET, []() {
        String html = "<html><body><h1>BPR Config</h1><form action='/save' method='post'>";
        html += "Base ID: <input name='base_id' required><br>";
        html += "WiFi SSID: <input name='ssid' required><br>";
        html += "WiFi Pass: <input name='pass' required><br>";
        html += "Firebase Project: <input name='project' required><br>";
        html += "Firebase URL: <input name='url' required><br>";
        html += "Firebase Key: <input name='key' required><br>";
        html += "<button type='submit'>Save</button></form></body></html>";
        server.send(200, "text/html", html);
    });
    
    server.on("/save", HTTP_POST, []() {
        HubConfig& config = configManager.getConfig();
        
        if (server.hasArg("base_id")) strcpy(config.base_id, server.arg("base_id").c_str());
        if (server.hasArg("ssid")) strcpy(config.wifi.ssid, server.arg("ssid").c_str());
        if (server.hasArg("pass")) strcpy(config.wifi.password, server.arg("pass").c_str());
        if (server.hasArg("project")) strcpy(config.firebase.project_id, server.arg("project").c_str());
        if (server.hasArg("url")) strcpy(config.firebase.database_url, server.arg("url").c_str());
        if (server.hasArg("key")) strcpy(config.firebase.api_key, server.arg("key").c_str());
        
        if (configManager.saveConfig()) {
            server.send(200, "text/html", "<html><body><h1>Saved! Restarting...</h1></body></html>");
            delay(1000);
            ESP.restart();
        } else {
            server.send(500, "text/html", "<html><body><h1>Error saving config</h1></body></html>");
        }
    });
}