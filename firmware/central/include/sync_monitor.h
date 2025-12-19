#pragma once
#include <Arduino.h>

namespace SyncMonitor {
    void recordFailure();
    void recordSuccess();
    bool shouldFallback();
    void reset();
}