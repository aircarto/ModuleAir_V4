#include <Arduino.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include "config.h"
#include "logger.h"
#include "i18n.h"
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
  i18nInit();          // load language from NVS (survives OTA) before any UI/screen text
  settingsInit();
  dataSenderInit();    // stamp provisioning AtmoSud + migration ancien flag (cf. data_sender.h)
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

  // Fire the first measurement cycle ~15 s after setup() finishes instead
  // of waiting a full DATA_SEND_INTERVAL (60 s). 15 s is enough for the
  // NextPM laser/fan + MH-Z19 UART + I2C sensors to settle into a usable
  // first reading; before that NextPM tends to return its NOT_READY flag
  // and the user would just see "PM ERR" anyway. By pre-loading lastCycle
  // we shift the first tick of `millis() - lastCycle >= DATA_SEND_INTERVAL`
  // forward without touching the interval itself.
  static const unsigned long WARMUP_MS = 15000;
  lastCycle = millis() - (DATA_SEND_INTERVAL - WARMUP_MS);
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

  // The "Config WiFi" slot is now part of the rotation itself (inserted by
  // displayUpdate() when wifiShouldShowConfigSplash() is true), so we no
  // longer need to gate the whole displayUpdate() to keep the AP screen
  // visible. The slot drops out automatically after 3 min.
  displayUpdate();
}
