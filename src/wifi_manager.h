#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

void wifiManagerInit();
void wifiManagerLoop();
bool wifiIsConnected();
bool wifiIsApMode();

// True while the matrix should keep showing the "Config WiFi" splash
// instead of normal data screens. Used by main.cpp to gate displayUpdate()
// during the first 3 minutes of AP-fallback mode.
bool wifiShouldShowConfigSplash();

void wifiSaveCredentialsAndRestart(const String& ssid, const String& password);

#endif
