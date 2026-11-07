#pragma once
#include <cstdint>
// Determinístico para os testes: sequência simples de "aleatórios"
inline uint32_t mockRandomState = 0x1234abcd;
inline uint32_t esp_random() {
    mockRandomState = mockRandomState * 1664525u + 1013904223u;
    return mockRandomState;
}
