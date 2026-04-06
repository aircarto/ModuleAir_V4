#ifndef BLE_IMPROV_H
#define BLE_IMPROV_H

#include <Arduino.h>

void bleImprovInit(const String& deviceName);
void bleImprovLoop();
void bleImprovStop();

// Called by wifi_manager when credentials are received via BLE
typedef void (*BleImprovCredentialsCallback)(const String& ssid, const String& password);
void bleImprovOnCredentials(BleImprovCredentialsCallback cb);

#endif
