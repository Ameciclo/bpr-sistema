/*
 * Test script for bike configuration flow
 * 
 * This simulates the bike configuration request/response flow:
 * 1. Bike sends: {"type": "config_request", "bike_id": "bpr-a1b2c3"}
 * 2. Hub checks authorization and responds with full config JSON
 * 3. Bike confirms: {"type": "config_received", "status": "ok"}
 */

#include <Arduino.h>
#include <ArduinoJson.h>

// Simulate bike config request
void testConfigRequest() {
    Serial.println("🧪 Testing bike configuration flow...");
    
    // Test 1: Authorized bike request
    String authorizedBike = "bpr-a1b2c3";
    DynamicJsonDocument request(256);
    request["type"] = "config_request";
    request["bike_id"] = authorizedBike;
    
    String requestStr;
    serializeJson(request, requestStr);
    Serial.printf("📤 Bike request: %s\n", requestStr.c_str());
    
    // Expected response format
    String expectedResponse = R"({
  "bike_id": "bpr-a1b2c3",
  "bike_name": "Bike bpr-a1b2c3", 
  "version": 1,
  "dev_mode": false,
  "wifi": {"scan_interval_sec": 300, "scan_timeout_ms": 5000},
  "ble": {"base_name": "BPR Hub Station", "scan_time_sec": 5},
  "power": {"deep_sleep_duration_sec": 3600},
  "battery": {"critical_voltage": 3.2, "low_voltage": 3.45}
})";
    
    Serial.printf("📥 Expected response: %s\n", expectedResponse.c_str());
    
    // Test 2: Unauthorized bike request
    String unauthorizedBike = "unknown-bike";
    request["bike_id"] = unauthorizedBike;
    serializeJson(request, requestStr);
    Serial.printf("📤 Unauthorized request: %s\n", requestStr.c_str());
    Serial.println("📥 Expected response: {\"error\":\"not_authorized\"}");
    
    // Test 3: Config confirmation
    DynamicJsonDocument confirmation(128);
    confirmation["type"] = "config_received";
    confirmation["bike_id"] = authorizedBike;
    confirmation["status"] = "ok";
    
    String confirmStr;
    serializeJson(confirmation, confirmStr);
    Serial.printf("📤 Bike confirmation: %s\n", confirmStr.c_str());
    
    Serial.println("✅ Test completed");
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    testConfigRequest();
}

void loop() {
    delay(1000);
}