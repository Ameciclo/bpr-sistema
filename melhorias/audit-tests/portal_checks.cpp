#include <iostream>
#include "config_manager.h"
#include "config_credentials.h"
#include "config_ap.h"
#include "WebServer.h"
ConfigManager configManager;ConfigCredentials configCredentials;
bool pendingRestart=false;uint32_t restartRequestTime=0;
int main(){
 ConfigAP::enter(true);
 mockFormArgs["config_json"]="{\"base_id\":\"base01\",\"wifi_ssid\":\"rede\",\"wifi_password\":\"senha\",\"firebase_database_url\":\"https://example.invalid\",\"firebase_api_key\":\"AIzaEXEMPLO\"}";
 mockRoutes["/save-json"]();
 std::cout<<"flat_json_response: "<<mockResponseCode<<"\n";
 std::cout<<"flat_json_api_key_saved: "<<(strlen(configCredentials.getCredentials().firebase_api_key)>0?"yes":"no")<<"\n";
 std::cout<<"flat_json_credentials_valid: "<<(configCredentials.isCredentialsValid()?"yes":"no")<<"\n";
}
