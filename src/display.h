#ifndef DISPLAY_H
#define DISPLAY_H

struct SensorData;

void displayInit();
void displayUpdate();
void displayShowLogo();
void displayShowDebugSplash();
bool displayGetDebugSplash();
void displaySetDebugSplash(bool enabled);
void displaySetSensorData(const SensorData& data);

// WiFi status screens
void displayShowWifiConnecting(const char* ssid);
void displayShowWifiDots();
void displayShowWifiConnected(const char* ssid, int rssi);
void displayShowAPMode(const char* apSSID, const char* apIP);
void displayShowWifiLost();
void displayShowWifiReconnected();

// OTA screens
void displayShowOtaUpdate();
void displayShowOtaProgress(int percent);
void displayShowOtaDone();
void displayShowOtaFailed();

#endif
