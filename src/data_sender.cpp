#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "sensors.h"
#include "data_sender.h"
#include "logger.h"

// Vérifie l'accès internet en résolvant un DNS fiable
static bool checkInternetAccess() {
  IPAddress ip;
  if (WiFi.hostByName("clients3.google.com", ip)) {
    Logger.printf("[Data] Internet OK (DNS resolved: %s)\n", ip.toString().c_str());
    return true;
  }
  Logger.println("[Data] WiFi connecte mais pas d'acces internet (DNS failed)");
  return false;
}

SendResult dataSenderSend() {
  const SensorData& d = sensorsGetData();

  // Ne pas envoyer si aucune mesure n'a été faite
  if (d.lastReadTime == 0) {
    Logger.println("[Data] Pas encore de donnees a envoyer");
    return SEND_OK;
  }

  // Vérifier la connectivité internet avant d'envoyer
  if (!checkInternetAccess()) {
    return SEND_NO_INTERNET;
  }

  // Parser la version firmware
  int vMajor = 0, vMinor = 0, vPatch = 0;
  sscanf(FIRMWARE_VERSION, "%d.%d.%d", &vMajor, &vMinor, &vPatch);

  // Construire le JSON
  String json = "{";
  json += "\"device_id\":\"" + deviceId + "\"";
  json += ",\"signal_quality\":" + String(WiFi.RSSI());
  json += ",\"signal_quality_unit\":\"" + String(WiFi.RSSI()) + " dB\"";
  json += ",\"version\":1";
  json += ",\"version_major\":" + String(vMajor);
  json += ",\"version_minor\":" + String(vMinor);
  json += ",\"version_patch\":" + String(vPatch);

  // Particules fines (NextPM)
  if (d.pm_ok) {
    json += ",\"ISO_68\":" + String(d.pm1, 1);
    json += ",\"ISO_68_unit\":\"" + String(d.pm1, 1) + " ugm3\"";
    json += ",\"ISO_39\":" + String(d.pm25, 1);
    json += ",\"ISO_39_unit\":\"" + String(d.pm25, 1) + " ugm3\"";
    json += ",\"ISO_24\":" + String(d.pm10, 1);
    json += ",\"ISO_24_unit\":\"" + String(d.pm10, 1) + " ugm3\"";
  }

  // Température, humidité, pression (BME280)
  if (d.bme_ok) {
    json += ",\"ISO_54\":" + String(d.temperature, 1);
    json += ",\"ISO_54_unit\":\"" + String(d.temperature, 1) + " °C\"";
    json += ",\"ISO_55\":" + String(d.humidity, 1);
    json += ",\"ISO_55_unit\":\"" + String(d.humidity, 1) + " %\"";
    json += ",\"ISO_53\":" + String(d.pressure, 1);
    json += ",\"ISO_53_unit\":\"" + String(d.pressure, 1) + " hPa\"";
  }

  // CO2 (MH-Z19)
  if (d.co2_ok) {
    json += ",\"ISO_17\":" + String(d.co2);
    json += ",\"ISO_17_unit\":\"" + String(d.co2) + " ppm\"";
  }

  // COV (CCS811)
  if (d.ccs_ok) {
    json += ",\"ISO_100\":" + String(d.tvoc);
    json += ",\"ISO_100_unit\":\"" + String(d.tvoc) + " ppb\"";
    json += ",\"ISO_101\":" + String(d.eco2);
    json += ",\"ISO_101_unit\":\"" + String(d.eco2) + " ppm\"";
  }

  // error_flags (bitmask)
  uint8_t errorFlags = 0;
  if (!d.bme_ok)  errorFlags |= 0x04;  // Bit 2 = BME280_ERROR
  if (!d.pm_ok)   errorFlags |= 0x08;  // Bit 3 = NPM_ERROR
  if (!d.ccs_ok)  errorFlags |= 0x10;  // Bit 4 = CCS811_ERROR (ENVEA_ERROR)
  if (!d.co2_ok)  errorFlags |= 0x80;  // Bit 7 = MHZ19_ERROR (WIND_ERROR)
  json += ",\"error_flags\":" + String(errorFlags);

  // npm_status (registre statut NextPM)
  json += ",\"npm_status\":" + String(d.npmStatus);

  // device_status (bitmask)
  uint8_t deviceStatus = 0;
  deviceStatus |= 0x02;  // Bit 1 = WIFI_CONNECTED (toujours vrai ici, on envoie que si connecté)
  if (millis() < 300000) deviceStatus |= 0x80;  // Bit 7 = BOOT_RECENT (uptime < 5 min)
  json += ",\"device_status\":" + String(deviceStatus);

  json += "}";

  Logger.println("[Data] POST " + String(DATA_SERVER_URL));
  Logger.println("[Data] " + json);

  WiFiClientSecure secClient;
  secClient.setInsecure();
  HTTPClient http;
  http.begin(secClient, DATA_SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  const char* headerKeys[] = {"Date"};
  http.collectHeaders(headerKeys, 1);
  http.setTimeout(10000);

  unsigned long t0 = millis();
  int httpCode = http.POST(json);
  float duration = (millis() - t0) / 1000.0;

  SendResult result = SEND_SERVER_DOWN;

  if (httpCode > 0) {
    Logger.printf("[Data] HTTP %d (%.1fs) - %s\n", httpCode, duration, http.getString().c_str());
    String serverDate = http.header("Date");
    if (serverDate.length() > 0) {
      Logger.printf("[Data] Server time: %s\n", serverDate.c_str());
    }
    if (httpCode >= 200 && httpCode < 300) {
      result = SEND_OK;
    }
  } else {
    Logger.printf("[Data] Internet OK mais serveur indisponible (%.1fs): %s\n", duration, http.errorToString(httpCode).c_str());
  }

  http.end();

  if (result == SEND_OK) {
    Logger.println("[Data] Envoi reussi");
  } else {
    Logger.println("[Data] Echec envoi - serveur data.moduleair.fr injoignable");
  }

  Logger.println();
  Logger.println("════════════════════════════════════════");
  Logger.printf("  Prochain envoi dans %ds\n", DATA_SEND_INTERVAL / 1000);
  Logger.println("════════════════════════════════════════");
  Logger.println();

  return result;
}
