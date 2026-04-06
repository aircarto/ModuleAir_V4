#include <Arduino.h>
#include <esp_mac.h>
#include "config.h"
#include "logger.h"

String deviceId;
String apSSID;

void configInit() {
  // Générer le device ID depuis le MAC
  uint64_t mac = ESP.getEfuseMac();
  deviceId = String((uint16_t)(mac >> 32), HEX);
  deviceId += String((uint32_t)mac, HEX);
  deviceId.toUpperCase();

  // SSID AP unique avec les 6 derniers caractères du MAC
  apSSID = "ModuleAirLight-" + deviceId.substring(deviceId.length() - 6);

  Logger.printf("[Config] Device ID: %s\n", deviceId.c_str());
  Logger.printf("[Config] AP SSID:   %s\n", apSSID.c_str());
}
