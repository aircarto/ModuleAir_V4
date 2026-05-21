#include <Arduino.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include "config.h"
#include "logger.h"
#include "display.h"
#include "wifi_manager.h"
#include "sensors.h"
#include "data_sender.h"
#include "settings.h"

unsigned long lastCycle = 0;
bool wasConnected = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Confirm OTA partition is valid (prevents rollback on next reboot)
  esp_ota_mark_app_valid_cancel_rollback();

  loggerInit();

  Logger.println("========================");
  Logger.println("  ModuleAir V4");
  Logger.print("  v");
  Logger.println(FIRMWARE_VERSION);
  Logger.println("========================");
  Logger.println();

  configInit();
  settingsInit();
  displayInit();
  displayShowBootAnim();

  if (displayGetDebugSplash()) {
    displayShowDebugSplash();
  }

  wifiManagerInit();
  sensorsInit();

  if (wifiIsConnected()) {
    Logger.println();
    Logger.println("Waiting for sensor warm-up...");
  } else {
    Logger.println();
    Logger.println("Mode AP - mesures locales actives");
  }
  Logger.println();

  wasConnected = wifiIsConnected();
}

void loop() {
  wifiManagerLoop();

  bool connected = wifiIsConnected();

  // Détection perte / retour WiFi
  if (wasConnected && !connected) {
    Logger.println("[WiFi] Deconnexion detectee");
    displayShowWifiLost();
    displaySetNetStatus(NET_NO_INTERNET);
  } else if (!wasConnected && connected) {
    Logger.println("[WiFi] Reconnexion detectee");
    displayShowWifiConnected(WiFi.SSID().c_str(), WiFi.RSSI());
    displayShowWifiReconnected();
  }
  wasConnected = connected;

  if (millis() - lastCycle >= DATA_SEND_INTERVAL) {
    lastCycle = millis();
    sensorsRead();
    displaySetSensorData(sensorsGetData());
    displayShowInterieur();
    if (connected) {
      SendResult r = dataSenderSend();
      displaySetNetStatus(r == SEND_NO_INTERNET ? NET_NO_INTERNET
                        : r == SEND_SERVER_DOWN ? NET_API_ERROR
                                                : NET_OK);
    } else {
      displaySetNetStatus(NET_NO_INTERNET);
    }
  }

  displayUpdate();
}
