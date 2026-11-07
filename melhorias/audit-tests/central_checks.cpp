#include <iostream>
#include "LittleFS.h"
#include "HTTPClient.h"
#include "config_manager.h"
#include "config_credentials.h"
#include "buffer_manager.h"
#include "ble_server.h"
#include "bpr_json_helper.h"
ConfigManager configManager;ConfigCredentials configCredentials;BufferManager bufferManager;
void BPRBLEServer::setBusyStatus(bool,uint32_t){}
#include "../bpr-sistema/firmware/central/src/bike_manager.cpp"
#include "../bpr-sistema/firmware/central/src/cloud_sync.cpp"
void result(const char*n,const std::string&v){std::cout<<n<<": "<<v<<"\n";}
int main(int argc,char**){
 if(argc>1){BufferFileHeader h{BUFFER_MAGIC,BUFFER_VERSION,11,0};BufferItemBin item{};mockFiles["/buffer/scan001.bin"]=std::string((char*)&h,sizeof h);for(int i=0;i<11;i++)mockFiles["/buffer/scan001.bin"].append((char*)&item,sizeof item);std::cout<<bufferManager.getTotalDataCount();return 0;}

 strcpy(configCredentials.getCredentials().base_id,"base01");strcpy(configCredentials.getCredentials().firebase_database_url,"https://example.invalid");
 BikeRegistryData registry{};registry.bike_count=1;registry.bikes[0]=123456;registry.statuses[0]=2;mockFiles[BIKE_REGISTRY_FILE]=std::string((char*)&registry,sizeof registry);
 BikeManager::init();result("allow_bike_from_downloaded_registry",BikeManager::canConnect("bpr-123456")?"yes":"no");result("runtime_status_count",std::to_string(bikeStatus.bike_count));
 for(int i=0;i<11;i++){BikeManager::updateBikeConfigFromCSV("bpr-123456","60,5000,20,-85,3600,5,0");}
 result("config_slots_after_11_updates_same_bike",std::to_string(bikeConfigs.bike_count));result("hasConfigUpdate_after_download",BikeManager::hasConfigUpdate("bpr-123456")?"true":"false");
 configManager.updateFromCSV("90,60,15,30,60,10000,15,100,0,0,0,300,1500,150,800,10,8000,5,30,3,300,50,80,5,256,1,64,20,10,0.5,1,24");result("zero_led_config_accepted",configManager.isConfigValid()?"yes":"no");result("central_config_last_update",std::to_string(configManager.getConfig().last_update));
 String raw="{\"x\":1}";bufferManager.addData("bpr-123456",(const uint8_t*)raw.c_str(),raw.size());bufferManager.addData("bpr-123456",(const uint8_t*)raw.c_str(),raw.size());
 result("buffer_files_after_two_adds",std::to_string(mockFiles.count("/buffer/scan001.bin")+mockFiles.count("/buffer/scan002.bin")));
 DynamicJsonDocument upload(4096);bufferManager.getDataForUpload(upload);int files=0;for(auto&x:mockFiles)if(x.first.rfind("/buffer/",0)==0)files++;result("buffer_files_after_upload_preparation",std::to_string(files));
 mockWriteFailure=true;result("addData_when_flash_write_fails",bufferManager.addData("bpr-123456",(const uint8_t*)raw.c_str(),raw.size())?"success":"failure");mockWriteFailure=false;
 result("network_packet_134_bytes_accepted",bufferManager.addData("bpr-123456",(const uint8_t*)std::string(134,'x').data(),134)?"yes":"no");
 mockHttpCalls.clear();bikeStatus.bike_count=0;CloudSync::enter();CloudSync::update();int bikeConfigCalls=0;for(auto&u:mockHttpCalls)if(u.find("bike_configs/")!=std::string::npos)bikeConfigCalls++;result("bike_config_downloads_first_sync",std::to_string(bikeConfigCalls));
 result("hex_id_central_roundtrip",intToBikeId(bikeIdToInt("bpr-ab12cd")));
}
