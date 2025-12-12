#!/usr/bin/env python3
"""
Simulador básico ESP32C3 para desenvolvimento BPR Sistema
Simula comportamentos principais sem hardware físico
"""

import time
import json
import threading
from datetime import datetime
from typing import Dict, List, Any

class ESP32Simulator:
    def __init__(self):
        self.pins = {8: False}  # LED_PIN
        self.wifi_connected = False
        self.ble_active = False
        self.config = {}
        self.buffer = []
        self.running = True
        
    def digitalWrite(self, pin: int, value: bool):
        """Simula digitalWrite"""
        self.pins[pin] = value
        status = "ON" if value else "OFF"
        print(f"[LED] Pin {pin}: {status}")
        
    def delay(self, ms: int):
        """Simula delay"""
        time.sleep(ms / 1000.0)
        
    def millis(self) -> int:
        """Simula millis()"""
        return int(time.time() * 1000)
        
    def load_config(self) -> Dict[str, Any]:
        """Simula carregamento de config"""
        try:
            with open('config.json', 'r') as f:
                self.config = json.load(f)
                print(f"[CONFIG] Loaded: {self.config}")
                return self.config
        except:
            print("[CONFIG] Using defaults")
            return {
                "base_id": "sim_base",
                "sync_interval_sec": 300,
                "led_pin": 8
            }
    
    def wifi_connect(self, ssid: str, password: str) -> bool:
        """Simula conexão WiFi"""
        print(f"[WIFI] Connecting to {ssid}...")
        time.sleep(2)  # Simula delay de conexão
        self.wifi_connected = True
        print("[WIFI] Connected!")
        return True
        
    def ble_start(self):
        """Simula início BLE"""
        print("[BLE] Starting server...")
        self.ble_active = True
        print("[BLE] Server active")
        
    def firebase_sync(self, data: List[Dict]) -> bool:
        """Simula sync Firebase"""
        print(f"[FIREBASE] Syncing {len(data)} records...")
        time.sleep(1)  # Simula upload
        print("[FIREBASE] Sync complete")
        return True
        
    def led_pattern(self, interval_ms: int, count: int = -1):
        """Simula padrões de LED"""
        def blink():
            blinks = 0
            while (count == -1 or blinks < count) and self.running:
                self.digitalWrite(8, True)
                self.delay(interval_ms // 2)
                self.digitalWrite(8, False)
                self.delay(interval_ms // 2)
                blinks += 1
                
        threading.Thread(target=blink, daemon=True).start()
        
    def simulate_bike_connection(self, bike_id: str):
        """Simula conexão de bike"""
        print(f"[BLE] Bike {bike_id} connected")
        # Simula recebimento de dados
        fake_data = {
            "bike_id": bike_id,
            "timestamp": self.millis(),
            "scans": [
                {"ssid": "NET_CLARO", "rssi": -65},
                {"ssid": "VIVO_FIBRA", "rssi": -72}
            ],
            "battery": 78
        }
        self.buffer.append(fake_data)
        print(f"[DATA] Received from {bike_id}: {len(fake_data['scans'])} scans")
        
    def run_simulation(self):
        """Executa simulação principal"""
        print("🚀 ESP32C3 Simulator - BPR Sistema")
        print("=" * 40)
        
        # Boot sequence
        print("[BOOT] Starting...")
        self.led_pattern(100, 5)  # Boot blink
        time.sleep(1)
        
        # Load config
        self.load_config()
        
        # Start BLE
        self.ble_start()
        self.led_pattern(2000)  # BLE ready blink
        
        # Simula bikes conectando
        time.sleep(3)
        self.simulate_bike_connection("bike07")
        time.sleep(2)
        self.simulate_bike_connection("bike12")
        
        # WiFi sync cycle
        time.sleep(5)
        if self.wifi_connect("Wokwi-GUEST", ""):
            self.led_pattern(500, 3)  # Sync blink
            self.firebase_sync(self.buffer)
            self.buffer.clear()
            
        print("\n[SIM] Simulation running... Press Ctrl+C to stop")
        
        try:
            while self.running:
                # Heartbeat
                print(f"[HEARTBEAT] {datetime.now().strftime('%H:%M:%S')} - {len(self.buffer)} buffered")
                time.sleep(10)
        except KeyboardInterrupt:
            print("\n[SIM] Stopping simulation...")
            self.running = False

if __name__ == "__main__":
    sim = ESP32Simulator()
    sim.run_simulation()