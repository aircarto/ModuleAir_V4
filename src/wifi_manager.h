#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

void wifiManagerInit();
void wifiManagerLoop();
bool wifiIsConnected();
bool wifiIsApMode();
void wifiSaveCredentialsAndRestart(const String& ssid, const String& password);

#endif
