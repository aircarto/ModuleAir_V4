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
#include "network_monitor.h"

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
  networkMonitorInit();   // background task: probes WiFi/internet/server every 15s

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

  // Détection perte / retour WiFi (purely for the WiFi-lost / -reconnected
  // splash screens). The connectivity BADGE is owned by the network monitor
  // task which probes every 15s — we don't touch displaySetNetStatus from
  // here anymore to avoid racing with it.
  if (wasConnected && !connected) {
    Logger.println("[WiFi] Deconnexion detectee");
    displayShowWifiLost();
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
      dataSenderSend();
    }
  }

  displayUpdate();
}
