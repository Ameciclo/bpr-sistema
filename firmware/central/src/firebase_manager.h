#pragma once
#include <Arduino.h>

bool uploadToFirebase(String path, String json);
bool downloadFromFirebase(String path, String& result);
bool sendHeartbeat();
bool uploadPendingData(String& pendingData);