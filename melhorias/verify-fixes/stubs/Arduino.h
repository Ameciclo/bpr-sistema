#pragma once
// Stubs de host para verificação nativa dos fontes do firmware BPR
// (revisão fixes/november — R01..R23). Não substituem teste em placa.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <type_traits>
#define HEX 16
#define OUTPUT 1
#define INPUT_PULLUP 2
#define HIGH 1
#define LOW 0
#define A0 2
using std::min;

inline uint32_t mockMillis = 10000;
inline uint64_t mockChip = 123456;
inline int mockAdc = 2300;

inline uint32_t millis() { return mockMillis; }
inline void delay(uint32_t n) { mockMillis += n; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int analogRead(int) { return mockAdc; }
inline bool getLocalTime(tm* t) { time_t n = time(nullptr); *t = *gmtime(&n); return true; }
inline void configTime(int, int, const char*) {}

class String : public std::string {
public:
  using std::string::string;
  String() : std::string() {}
  String(const std::string& s) : std::string(s) {}
  template<class T, typename std::enable_if<std::is_arithmetic<T>::value, int>::type = 0>
  String(T v) : std::string(std::to_string(v)) {}
  // Base explícita para inteiros sem sinal (comportamento igual ao ESP32,
  // onde uint32_t == unsigned long) — sem isso o host cairia no ctor
  // aritmético e produziria decimal quando o alvo produz hex
  String(unsigned long v, int base) { char b[40]; snprintf(b, sizeof b, base == 16 ? "%lx" : "%lu", v); assign(b); }
  String(unsigned int v, int base) { char b[40]; snprintf(b, sizeof b, base == 16 ? "%x" : "%u", v); assign(b); }
  bool isEmpty() const { return empty(); }
  char charAt(size_t i) const { return (*this)[i]; }
  bool startsWith(const String& s) const { return compare(0, s.size(), s) == 0; }
  bool endsWith(const String& s) const { return size() >= s.size() && compare(size() - s.size(), s.size(), s) == 0; }
  int indexOf(const String& s, size_t p = 0) const { auto n = find(s, p); return n == npos ? -1 : (int)n; }
  int indexOf(char s, size_t p = 0) const { auto n = find(s, p); return n == npos ? -1 : (int)n; }
  int lastIndexOf(char c) const { auto n = rfind(c); return n == npos ? -1 : (int)n; }
  String substring(size_t a, size_t b = npos) const { if (a > size()) return ""; return substr(a, b == npos ? npos : b - a); }
  long toInt() const { return strtol(c_str(), nullptr, 10); }
  float toFloat() const { return strtof(c_str(), nullptr); }
  void replace(const String& a, const String& b) { size_t p = 0; while ((p = find(a, p)) != npos) { std::string::replace(p, a.size(), b); p += b.size(); } }
  void reserve(size_t n) { std::string::reserve(n); }
};

struct MockSerial {
  void begin(int) {}
  template<class... T> void printf(const char*, T...) {}
  template<class T> void println(T) {}
  void println() {}
  template<class T> void print(T) {}
};
inline MockSerial Serial;

struct MockESP {
  uint32_t getFreeHeap() { return 200000; }
  uint64_t getEfuseMac() { return mockChip; }
  void restart() {}
};
inline MockESP ESP;
