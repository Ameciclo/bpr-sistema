#pragma once
#include "Arduino.h"
class NimBLEClient;
class NimBLEClientCallbacks {public:virtual void onDisconnect(NimBLEClient*){}};
class NimBLEAdvertisedDevice {public:std::string getName(){return "BPR Central";}};
class NimBLEScanResults {public:int getCount(){return 0;}NimBLEAdvertisedDevice getDevice(int){return {};}};
class NimBLEScan {public:void setActiveScan(bool){}NimBLEScanResults start(int s,bool){delay(s*1000);return {};}void clearResults(){}};
inline std::vector<std::string> mockBleWrites;
inline bool mockBleWriteResult=true;
class NimBLERemoteCharacteristic {public:bool writeValue(const char*s){mockBleWrites.push_back(s);return mockBleWriteResult;}};
class NimBLERemoteService {public:NimBLERemoteCharacteristic*getCharacteristic(const char*){static NimBLERemoteCharacteristic c;return &c;}};
class NimBLEClient {public:bool connect(NimBLEAdvertisedDevice*){return true;}void disconnect(){}bool isConnected(){return true;}NimBLERemoteService*getService(const char*){static NimBLERemoteService s;return &s;}};
#define ESP_PWR_LVL_P3 3
struct ble_gap_conn_desc {uint16_t conn_handle;int peer_id_addr;};
class NimBLEAddress {public:NimBLEAddress(int=0){}std::string toString(){return "00:11:22:33:44:55";}};
class NimBLEServer;class NimBLECharacteristic;
class NimBLEServerCallbacks {public:virtual void onConnect(NimBLEServer*,ble_gap_conn_desc*){}virtual void onDisconnect(NimBLEServer*,ble_gap_conn_desc*){}};
class NimBLECharacteristicCallbacks {public:virtual void onWrite(NimBLECharacteristic*){}};
class NimBLECharacteristic {public:NimBLECharacteristicCallbacks* cb=nullptr;std::string value;int notifyCalls=0;void setCallbacks(NimBLECharacteristicCallbacks*c){cb=c;}std::string getValue(){return value;}void setValue(const char*s){value=s;}void notify(){notifyCalls++;}};
namespace NIMBLE_PROPERTY {enum {READ=1,WRITE=2,NOTIFY=4};}
class NimBLEService {public:NimBLECharacteristic*createCharacteristic(const char*,int){return new NimBLECharacteristic;}void start(){}};
class NimBLEAdvertising {public:void stop(){}void addServiceUUID(const char*){}void setScanResponse(bool){}void reset(){}void setName(const char*){}void start(){}bool isAdvertising(){return true;}};
class NimBLEServer {public:NimBLEServerCallbacks*cb=nullptr;void setCallbacks(NimBLEServerCallbacks*c){cb=c;}NimBLEService*createService(const char*){return new NimBLEService;}NimBLEAdvertising*getAdvertising(){static NimBLEAdvertising a;return &a;}void disconnect(uint16_t){}};
struct NimBLEDevice{
 static void init(const char*){}static NimBLEScan*getScan(){static NimBLEScan s;return &s;}static NimBLEClient*createClient(){return new NimBLEClient;}
 static void setPower(int){}static NimBLEServer*createServer(){return new NimBLEServer;}static NimBLEAdvertising*getAdvertising(){static NimBLEAdvertising a;return &a;}static void startAdvertising(){}static void deinit(bool){}static bool getInitialized(){return true;}static NimBLEAddress getAddress(){return {};}
};
