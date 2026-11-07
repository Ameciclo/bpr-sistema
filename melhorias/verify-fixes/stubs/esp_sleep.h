#pragma once
#include <cstdint>

// X01: stubs acompanham o contrato real do IDF — o wakeup agora retorna
// esp_err_t e o firmware DECIDE com base no retorno (dormir sem wakeup
// armado seria brick)
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t) { return ESP_OK; }
inline void esp_deep_sleep_start() {}
