#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "logger.h"
#include "display.h"
#include "wifi_manager.h"
#include "sensors.h"
#include "data_sender.h"
#include "settings.h"

void setup() {
  Serial.begin(115200);
  delay(1000);
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

  if (displayGetDebugSplash()) {
    displayShowDebugSplash();
  }

  wifiManagerInit();

  if (wifiIsConnected()) {
    sensorsInit();
    Logger.println();
    Logger.println("Waiting for sensor warm-up...");
  } else {
    Logger.println();
    Logger.println("Waiting for WiFi configuration...");
  }
  Logger.println();
}

unsigned long lastCycle = 0;
bool wasConnected = true;  // avoid false "reconnection" on first loop

void loop() {
  wifiManagerLoop();

  bool connected = wifiIsConnected();

  // Détection perte / retour WiFi
  if (wasConnected && !connected) {
    Logger.println("[WiFi] Deconnexion detectee");
    displayShowWifiLost();
  } else if (!wasConnected && connected) {
    Logger.println("[WiFi] Reconnexion detectee");
    displayShowWifiConnected(WiFi.SSID().c_str(), WiFi.RSSI());
    displayShowWifiReconnected();
  }
  wasConnected = connected;

  if (connected && millis() - lastCycle >= DATA_SEND_INTERVAL) {
    lastCycle = millis();
    sensorsRead();
    dataSenderSend();
    displaySetSensorData(sensorsGetData());
  }

  displayUpdate();
}
