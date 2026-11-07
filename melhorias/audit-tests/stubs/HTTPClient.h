#pragma once
#include "Arduino.h"
#define HTTP_CODE_OK 200
inline std::map<std::string,String> mockHttpResponses;
inline std::vector<String> mockHttpCalls;
class HTTPClient {String url;public:void begin(const String&u){url=u;mockHttpCalls.push_back(u);}int GET(){return 200;}String getString(){return mockHttpResponses[url];}void end(){}void addHeader(const char*,const char*){}int PATCH(const String&s){mockHttpCalls.push_back(s);return 200;}int PUT(const String&s){mockHttpCalls.push_back(s);return 200;}};
