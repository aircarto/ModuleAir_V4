#ifndef DISPLAY_H
#define DISPLAY_H

struct SensorData;

void displayInit();
void displayUpdate();
void displayShowBootAnim();
void displayShowLogo();
void displayShowInterieur();
void displayShowDebugSplash();
bool displayGetDebugSplash();
void displaySetDebugSplash(bool enabled);
void displaySetSensorData(const SensorData& data);
void displaySetBrightness(uint8_t brightness);
uint8_t displayGetBrightness();

// WiFi status screens
void displayShowWifiConnecting(const char* ssid);
void displayShowWifiDots();
void displayShowWifiConnected(const char* ssid, int rssi);
void displayShowAPMode(const char* apSSID, const char* apIP);
void displayShowWifiLost();
void displayShowWifiReconnected();

// BLE provisioning screens
void displayShowBleConnected();
void displayShowBleCredentials(const char* ssid);
void displayShowBleWifiTrying(const char* ssid);
void displayShowBleWifiOk(const char* ssid);
void displayShowBleWifiFail();
void displayShowBleReboot();

// OTA screens
void displayShowOtaUpdate();
void displayShowOtaProgress(int percent);
void displayShowOtaDone();
void displayShowOtaFailed();

// Connectivity badge shown at the top-right of measurement screens
enum NetStatus {
  NET_OK,           // WiFi + internet + last API send all OK → blue WiFi icon
  NET_NO_INTERNET,  // no WiFi or DNS fails → red WiFi-with-dashes icon
  NET_API_ERROR     // WiFi+internet OK but server unreachable → red up/down arrows
};
void displaySetNetStatus(NetStatus s);

#endif
