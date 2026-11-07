#pragma once
// Mock de LittleFS para host: arquivos em std::map com gravação POSICIONAL
// (seek + write sobrescreve, como no alvo) — necessário para os testes de
// append/atualização in-place do BufferManager.
#include "Arduino.h"
#include <map>
#include <vector>

inline std::map<std::string, std::string> mockFiles;
inline bool mockWriteFailure = false;
// N07: falha direcionada à atualização in-place da flag `uploaded` — a
// única escrita de 1 byte do código em teste (setItemUploadedFlag).
// mockSingleByteWriteFailure: TODAS as escritas de 1 byte falham;
// mockSingleByteWriteBudget >= 0: as N primeiras passam, depois falham
// (simula o pior caso real: UMA flag marcada, a seguinte falha)
inline bool mockSingleByteWriteFailure = false;
inline int mockSingleByteWriteBudget = -1;

class File {
  std::string path;
  size_t pos = 0;
  bool valid = false;
  bool dir = false;
  std::vector<std::string> entries;
  size_t next = 0;

public:
  File() {}
  File(std::string p, bool v, bool d = false) : path(p), valid(v), dir(d) {
    if (dir) for (auto& kv : mockFiles) if (kv.first.rfind(p + "/", 0) == 0) entries.push_back(kv.first);
  }
  operator bool() const { return valid; }
  int read() { char c; return readBytes(&c, 1) ? (unsigned char)c : -1; }
  size_t read(uint8_t* dst, size_t n) { return readBytes((char*)dst, n); }
  size_t readBytes(char* dst, size_t n) {
    auto it = mockFiles.find(path);
    if (it == mockFiles.end()) return 0;
    const std::string& s = it->second;
    if (pos >= s.size()) return 0;
    n = std::min(n, s.size() - pos);
    memcpy(dst, s.data() + pos, n);
    pos += n;
    return n;
  }
  size_t write(const uint8_t* src, size_t n) {
    if (mockWriteFailure) return 0;
    if (n == 1) {
      if (mockSingleByteWriteFailure) return 0;
      if (mockSingleByteWriteBudget == 0) return 0;
      if (mockSingleByteWriteBudget > 0) mockSingleByteWriteBudget--;
    }
    auto it = mockFiles.find(path);
    if (it == mockFiles.end()) return 0;
    std::string& s = it->second;
    if (pos > s.size()) s.resize(pos, '\0'); // seek além do fim preenche com zeros
    s.replace(pos, n, (const char*)src, n);
    pos += n;
    return n;
  }
  size_t write(uint8_t c) { return write(&c, 1); }
  bool seek(size_t p) { pos = p; return true; }
  void close() {}
  size_t size() { auto it = mockFiles.find(path); return it == mockFiles.end() ? 0 : it->second.size(); }
  const char* name() { static std::string base; base = path.substr(path.find_last_of('/') + 1); return base.c_str(); }
  File openNextFile() { return next < entries.size() ? File(entries[next++], true) : File(); }
  void println(const char* s) { write((const uint8_t*)s, strlen(s)); }
  String readString() { auto it = mockFiles.find(path); return it == mockFiles.end() ? String() : String(it->second); }
};

struct MockFS {
  bool begin(bool = false) { return true; }
  bool format() { mockFiles.clear(); return true; }
  bool exists(const String& p) {
    if (p == "/buffer" || p == "/backup") return true;
    return mockFiles.count(p) > 0;
  }
  bool mkdir(const String&) { return true; }
  File open(const String& p, const char* mode = "r") {
    bool isDir = (p == "/buffer" || p == "/backup" || p == "/");
    if (mode[0] == 'w') {
      if (mockWriteFailure) return File();
      mockFiles[p] = "";
    } else if (mode[0] == 'r') {
      // "r" e "r+": "r+" abre existente sem truncar; inexistente é inválido
      if (mockFiles.count(p) == 0 && !isDir) return File();
    }
    return File(p, isDir || mockFiles.count(p), isDir);
  }
  bool remove(const String& p) { return mockFiles.erase(p) > 0; }
  bool rename(const String& from, const String& to) {
    auto it = mockFiles.find(from);
    if (it == mockFiles.end()) return false;
    mockFiles[to] = it->second;
    mockFiles.erase(it);
    return true;
  }
  size_t totalBytes() { return 983040; }
  size_t usedBytes() { size_t n = 0; for (auto& x : mockFiles) n += x.second.size(); return n; }
};
inline MockFS LittleFS;
