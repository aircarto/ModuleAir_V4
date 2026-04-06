#include <Arduino.h>
#include "config.h"
#include "logger.h"
#include "led.h"
#include "wifi_manager.h"
#include "sensors.h"
#include "data_sender.h"

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
  ledInit();
  wifiManagerInit();

  if (wifiIsConnected()) {
    ledStartConnectedAnim();
    sensorsInit();
    Logger.println();
    Logger.println("Waiting for sensor warm-up...");
  } else {
    ledSetApMode(true);
    Logger.println();
    Logger.println("Waiting for WiFi configuration...");
  }
  Logger.println();
}

unsigned long lastCycle = 0;
bool wasConnected = false;

void loop() {
  ledUpdate();
  wifiManagerLoop();

  bool connected = wifiIsConnected();

  // Détection perte / retour WiFi
  if (wasConnected && !connected) {
    Logger.println("[WiFi] Deconnexion detectee - LEDs damier rouge");
    ledSetWifiLost(true);
  } else if (!wasConnected && connected) {
    Logger.println("[WiFi] Reconnexion detectee");
    ledSetWifiLost(false);
    ledStartConnectedAnim();
  }
  wasConnected = connected;

  if (connected && millis() - lastCycle >= DATA_SEND_INTERVAL) {
    lastCycle = millis();
    ledTriggerPulse();
    sensorsRead();
    SendResult result = dataSenderSend();
    ledSetNoInternet(result == SEND_NO_INTERNET);
    ledSetServerDown(result == SEND_SERVER_DOWN);
  }
}
