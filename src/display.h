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

// Connectivity badge shown at the top-right of measurement screens.
// Three states map cleanly to the user's mental model:
//   - WiFi off            -> no badge (clean look, "we're offline")
//   - WiFi on, send OK    -> blue WiFi icon (everything works)
//   - WiFi on, send KO    -> red up/down arrows (data can't reach server,
//                            whatever the reason: no DNS, server down, 5xx)
enum NetStatus {
  NET_OFFLINE,  // WiFi disconnected — badge hidden
  NET_OK,       // WiFi connected and last data send succeeded
  NET_ERROR     // WiFi connected but data didn't reach the server
};
void displaySetNetStatus(NetStatus s);

#endif
