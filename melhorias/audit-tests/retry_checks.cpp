#include <iostream>
#include "Arduino.h"
#include "WiFi.h"
#include "config_manager.h"
#include "cloud_sync.h"
#include "config_ap.h"
#include "constants.h"
extern SystemState currentState;
extern ConfigManager configManager;
void changeState(SystemState);void loop();
void ConfigAP::enter(bool){}void ConfigAP::update(){}void ConfigAP::exit(){}void ConfigAP::printStatus(){}void ConfigAP::setupWebServer(){}
int main(){
 mockMillis=80000;mockWifiStatus=0;
 configManager.getConfig().timeouts.wifi_sec=1;
 changeState(STATE_CLOUD_SYNC);
 for(int i=0;i<5;i++){uint32_t start=millis();loop();std::cout<<"failure_loop_"<<i+1<<": state="<<getStateName(currentState)<<", elapsed_ms="<<millis()-start<<"\n";}
}
