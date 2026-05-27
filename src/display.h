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

// Connectivity badge (top-right of measurement screens). 4 states, each
// composable from two independent network-monitor probes:
//
//   - WiFi off                          -> no badge
//   - WiFi up + internet OK + server OK -> blue WiFi icon
//   - WiFi up + no internet             -> both arrows RED (uplink AND downlink broken)
//   - WiFi up + internet OK + server KO -> up arrow RED, down arrow GREEN
//                                          (we can reach the internet but our
//                                           server doesn't answer)
//
// Convention: the UP arrow is uplink (data going to our server) and the
// DOWN arrow is downlink (general internet reachability). Pattern inspired
// by ESPHome's layered status_binary_sensor.
enum NetStatus {
  NET_OFFLINE,      // WiFi disconnected — badge hidden
  NET_OK,           // Everything works
  NET_NO_INTERNET,  // WiFi up but no internet (both arrows red)
  NET_NO_SERVER     // Internet OK but our server unreachable (up red, down green)
};
void displaySetNetStatus(NetStatus s);

#endif
