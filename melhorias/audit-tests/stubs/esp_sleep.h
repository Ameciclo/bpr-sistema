#pragma once
inline int mockDeepSleepCalls=0;
inline void esp_sleep_enable_timer_wakeup(unsigned long long){}
inline void esp_deep_sleep_start(){mockDeepSleepCalls++;}
