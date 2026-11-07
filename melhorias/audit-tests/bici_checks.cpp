#include <iostream>
#include "LittleFS.h"
#include "WiFi.h"
#include "esp_sleep.h"
#include "ArduinoJson.h"
#define private public
#include "at_base.h"
#undef private
#include "scanning.h"
#include "power_manager.h"
extern ConfigManager configManager;
extern BufferManager bufferManager;
extern BikeState currentState;
void setup();void loop();
void result(const char*n,const std::string&v){std::cout<<n<<": "<<v<<"\n";}
int main(){
 setup();for(int i=0;i<6;i++)loop();
 result("boot_after_6_loops",std::to_string(currentState));result("wifi_scans_after_6_loops",std::to_string(mockScans));
 ConfigManager cfg;cfg.generateUniqueId();cfg.getConfig().wifi.scan_interval_sec=42;cfg.save();
 ConfigManager reboot;reboot.generateUniqueId();reboot.load();result("saved_interval_after_boot_sequence",std::to_string(reboot.getConfig().wifi.scan_interval_sec));
 BufferManager b;b.startSession("bpr-123456");NetworkData n{};strcpy(n.ssid,"rede");strcpy(n.bssid,"AA:BB:CC:DD:EE:FF");n.rssi=-60;n.channel=6;
 b.addScan(1000,&n,1);String json=b.toJson();result("one_network_payload",json);result("one_network_payload_bytes",std::to_string(json.size()));
 b.endSession();mockFiles.erase("/session.bin");b.save();result("saved_after_endSession",LittleFS.exists("/session.bin")?"yes":"no");
 b.startSession("bpr-123456");b.addScan(1000,&n,1);b.save();BufferManager restored;restored.load();result("loaded_before_startSession",restored.isEmpty()?"empty":"data");restored.startSession("bpr-123456");result("loaded_after_startSession",restored.isEmpty()?"empty":"data");
 b.clear();b.addScan(2000,&n,1);result("addScan_after_clear",b.isEmpty()?"empty":"data");
 PowerManager power;result("heartbeat_payload",power.getBatteryReport(cfg.getConfig()));
 cfg.processUpdate("{\"wifi\":{\"scan_interval_sec\":60},\"dev_mode\":false}");result("dev_mode_after_false_update",cfg.getConfig().dev_mode?"true":"false");
 BufferManager scanbuf;scanbuf.startSession("bpr-123456");ScanningState scan(cfg,scanbuf);mockMillis=400000;scan.update();DynamicJsonDocument sd(8192);deserializeJson(sd,scanbuf.toJson().c_str());result("scan_records_for_3_networks",std::to_string(sd["scans"].size()));result("networks_in_first_record",std::to_string(sd["scans"][0][1].size()));
 AtBaseState at(cfg,scanbuf);NimBLEAdvertisedDevice device;at.connectToBase(&device);mockBleWriteResult=false;mockMillis+=6000;at.update();result("buffer_after_failed_write",scanbuf.isEmpty()?"empty":"data");
 for(auto chip:{123456ULL,10000000ULL,10000001ULL,16777215ULL}){mockChip=chip;result((std::string("id_for_")+std::to_string(chip)).c_str(),generateBikeId());}
 result("session_struct_bytes",std::to_string(sizeof(SessionData)));
}
