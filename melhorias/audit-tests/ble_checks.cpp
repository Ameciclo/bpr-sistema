#include <iostream>
#include "bike_manager.h"
#include "../bpr-sistema/firmware/central/src/ble_server.cpp"
bool BikeManager::hasConfigUpdate(const String&){return false;}
String BikeManager::getConfigForBike(const String&){return "{}";}
void BikeManager::markConfigSent(const String&){}
int arrivals=0;
void arrived(const String&){arrivals++;}
void result(const char*n,const std::string&v){std::cout<<n<<": "<<v<<"\n";}
int main(){
 BPRBLEServer::start();BPRBLEServer::setConnectCallback(arrived);
 ble_gap_conn_desc d{0,0};BPRBLEServer::pServer->cb->onConnect(BPRBLEServer::pServer,&d);
 BPRBLEServer::pDataChar->setValue("{\"bike_id\":\"bpr-123456\",\"type\":\"heartbeat\"}");BPRBLEServer::pDataChar->cb->onWrite(BPRBLEServer::pDataChar);
 result("handle_zero_identified",BPRBLEServer::connectedDevices[0].isEmpty()?"no":"yes");result("handle_zero_timeout_removed",BPRBLEServer::connectionTimeouts.count(0)?"no":"yes");
 BPRBLEServer::connectedDevices.clear();BPRBLEServer::connectedDevices[4]="";BPRBLEServer::connectedDevices[5]="";
 BPRBLEServer::pDataChar->cb->onWrite(BPRBLEServer::pDataChar);BPRBLEServer::pDataChar->cb->onWrite(BPRBLEServer::pDataChar);
 result("first_empty_handle_assigned_without_sender",BPRBLEServer::connectedDevices[4]);result("arrival_callbacks_for_two_writes_same_bike",std::to_string(arrivals));
 BPRBLEServer::connectedDevices[5]="bpr-654321";BPRBLEServer::sendConfigToHandle(4,"bpr-123456","{\"wifi\":{\"scan_interval_sec\":60}}");result("notification_calls",std::to_string(BPRBLEServer::pConfigChar->notifyCalls));
}
