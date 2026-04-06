#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

void wifiManagerInit();
void wifiManagerLoop();
bool wifiIsConnected();
bool wifiIsApMode();

#endif
