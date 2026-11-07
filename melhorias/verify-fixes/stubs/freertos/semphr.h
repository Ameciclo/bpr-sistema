#pragma once
#include <cstdint>
// Mutex real de mock: take falha se já tomado (expõe vazamento de lock — R10)
inline int pdTRUE = 1;
inline int pdFALSE = 0;
inline uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
struct MockSemaphore { bool taken = false; };
typedef void* SemaphoreHandle_t;
inline void* xSemaphoreCreateMutex() { return new MockSemaphore(); }
inline int xSemaphoreTake(void* h, uint32_t) {
    auto* m = (MockSemaphore*)h;
    if (m->taken) return pdFALSE;
    m->taken = true;
    return pdTRUE;
}
inline int xSemaphoreGive(void* h) {
    auto* m = (MockSemaphore*)h;
    if (!m->taken) return pdFALSE;
    m->taken = false;
    return pdTRUE;
}
