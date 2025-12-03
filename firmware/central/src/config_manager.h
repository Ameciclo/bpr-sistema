#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include "structs.h"

// Funções de configuração
bool downloadConfigs();
bool loadConfigCache();
bool saveConfigCache();
GlobalConfig getGlobalConfig();
BaseConfig getBaseConfig();
bool isConfigValid();
void invalidateConfig();

#endif