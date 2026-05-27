#include <Arduino.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include "config.h"
#include "logger.h"
#include "display.h"
#include "logo_storage.h"
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
  logoStorageInit();
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
    displaySetNetStatus(NET_OFFLINE);   // no badge while WiFi is down
  } else if (!wasConnected && connected) {
    Logger.println("[WiFi] Reconnexion detectee");
    displayShowWifiConnected(WiFi.SSID().c_str(), WiFi.RSSI());
    displayShowWifiReconnected();
    displaySetNetStatus(NET_OK);        // optimistic until next send confirms
  }
  wasConnected = connected;

  if (millis() - lastCycle >= DATA_SEND_INTERVAL) {
    lastCycle = millis();
    sensorsRead();
    displaySetSensorData(sensorsGetData());
    displayShowInterieur();
    if (connected) {
      // Send result -> badge: any failure (no DNS / server down / 5xx) is
      // collapsed to NET_ERROR (red arrows). The user doesn't need to
      // distinguish "no DNS" from "server down" on a 8 px badge.
      SendResult r = dataSenderSend();
      displaySetNetStatus(r == SEND_OK ? NET_OK : NET_ERROR);
    } else {
      displaySetNetStatus(NET_OFFLINE);
    }
  }

  displayUpdate();
}
