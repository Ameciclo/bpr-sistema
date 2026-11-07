#pragma once
#include "Arduino.h"
#include <functional>
#define HTTP_GET 1
#define HTTP_POST 2
inline std::map<String,std::function<void()>> mockRoutes;
inline std::map<String,String> mockFormArgs;
inline int mockResponseCode=0;
class WebServer {public:WebServer(int){}void on(const char*s,int,std::function<void()> fn){mockRoutes[s]=fn;}void begin(){}void stop(){}void handleClient(){}bool hasArg(const char*s){return mockFormArgs.count(s);}String arg(const char*s){return mockFormArgs[s];}void send(int code,const char*,const String&){mockResponseCode=code;}};
