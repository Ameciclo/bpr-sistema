#pragma once
#include "Arduino.h"
class CRC32 {public:void update(const uint8_t*,size_t){}uint32_t finalize(){return 1;}};
