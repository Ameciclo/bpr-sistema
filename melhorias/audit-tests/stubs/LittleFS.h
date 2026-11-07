#pragma once
#include "Arduino.h"
#include <memory>
struct MockFS;
inline std::map<std::string,std::string> mockFiles;
inline bool mockWriteFailure=false;
class File {
 std::string path; size_t pos=0; bool valid=false; std::vector<std::string> entries;size_t next=0;
public:
 File(){}
 File(std::string p,bool v,bool dir=false):path(p),valid(v){if(dir)for(auto &kv:mockFiles)if(kv.first.rfind(p+"/",0)==0)entries.push_back(kv.first);}
 operator bool()const{return valid;}
 int read(){char c;return readBytes(&c,1)?(unsigned char)c:-1;}
 size_t readBytes(char* dst,size_t n){auto& s=mockFiles[path];n=std::min(n,s.size()-std::min(pos,s.size()));memcpy(dst,s.data()+pos,n);pos+=n;return n;}
 size_t write(const uint8_t* src,size_t n){if(mockWriteFailure)return 0;mockFiles[path].append((const char*)src,n);return n;}
 size_t write(uint8_t c){return write(&c,1);}
 void close(){}
 size_t size(){return mockFiles[path].size();}
 const char* name(){static std::string base;base=path.substr(path.find_last_of('/')+1);return base.c_str();}
 File openNextFile(){return next<entries.size()?File(entries[next++],true):File();}
 void println(const char* s){write((const uint8_t*)s,strlen(s));}
 String readString(){return mockFiles[path];}
};
struct MockFS {
 bool begin(bool=false){return true;}
 bool format(){mockFiles.clear();return true;}
 bool exists(const String&p){return p=="/buffer"||p=="/backup"||mockFiles.count(p);}
 bool mkdir(const String&){return true;}
 File open(const String&p,const char* mode="r"){bool dir=(p=="/buffer"||p=="/backup"||p=="/");if(mode[0]=='w'){if(mockWriteFailure)return File();mockFiles[p]="";}return File(p,dir||mockFiles.count(p),dir);}
 bool remove(const String&p){return mockFiles.erase(p);}
 size_t totalBytes(){return 1000000;}
 size_t usedBytes(){size_t n=0;for(auto&x:mockFiles)n+=x.second.size();return n;}
};
inline MockFS LittleFS;
