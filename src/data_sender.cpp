#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "sensors.h"
#include "data_sender.h"
#include "logger.h"
#include "settings.h"

// Vérifie l'accès internet en résolvant un DNS fiable
static bool checkInternetAccess() {
  IPAddress ip;
  if (WiFi.hostByName("clients3.google.com", ip)) {
    Logger.printf("[Net] Internet OK (DNS resolved: %s)\n", ip.toString().c_str());
    return true;
  }
  Logger.println("[Net] WiFi connecte mais pas d'acces internet (DNS failed)");
  return false;
}

// ── Envoi AtmoSud (MicroSpot) ───────────────────────────────────────────────
// Reproduit trait pour trait le comportement de ModuleAir-Next-Gen :
//   - identifiant "moduleairid" = chip id court (16 bits hauts de l'eFuse MAC,
//     en hexa) — exactement comme esp_chipid dans Next-Gen
//   - payload au format "sensordatavalues" (paires value_type / value)
//   - mêmes value_type que ceux attendus par l'API AtmoSud
//   - headers Content-Type + X-Sensor + User-Agent identiques
// Compilé dans TOUTES les builds : l'appel est conditionné à l'exécution par le
// flag NVS settingsGetAtmosudEnabled() (cf. dataSenderSend plus bas), pour que
// le choix « envoyer à AtmoSud » persiste à travers l'OTA quelle que soit la
// variante du binaire servi.
static void atmosudSend(const SensorData& d) {
  // chip id court, identique à esp_chipid de Next-Gen
  String chipid = String((uint16_t)(ESP.getEfuseMac() >> 32), HEX);

  String payload = "{\"moduleairid\": \"";
  payload += chipid;
  payload += "\", \"software_version\": \"" ATMOSUD_SOFTWARE_VERSION "\", \"sensordatavalues\":[";

  bool first = true;
  auto add = [&](const char* type, const String& val) {
    if (!first) payload += ",";
    payload += "{\"value_type\":\"";
    payload += type;
    payload += "\",\"value\":\"";
    payload += val;
    payload += "\"}";
    first = false;
  };

  // Particules fines (NextPM) — mêmes clés que Next-Gen
  if (d.pm_ok) {
    add("NPM_P0", String(d.pm1, 2));   // PM1
    add("NPM_P1", String(d.pm10, 2));  // PM10
    add("NPM_P2", String(d.pm25, 2));  // PM2.5
  }
  // BME280 — pression en Pa (×100) comme l'envoyait Next-Gen
  if (d.bme_ok) {
    add("BME280_temperature", String(d.temperature, 2));
    add("BME280_pressure",    String(d.pressure * 100.0, 2));
    add("BME280_humidity",    String(d.humidity, 2));
  }
  // CO2 (MH-Z19) — la clé reste "MHZ16_CO2", comme dans Next-Gen
  if (d.co2_ok) {
    add("MHZ16_CO2", String(d.co2));
  }
  // COV (CCS811)
  if (d.ccs_ok) {
    add("CCS811_VOC", String(d.tvoc));
  }
  // Qualité du signal WiFi (footer Next-Gen)
  add("signal", String(WiFi.RSSI()));

  payload += "]}";

  Logger.println("[AtmoSud] POST " + String(ATMOSUD_SERVER_URL));
  Logger.println("[AtmoSud] " + payload);

  WiFiClientSecure secClient;
  secClient.setInsecure();
  HTTPClient https;
  https.setTimeout(20000);
  https.setUserAgent(String(ATMOSUD_SOFTWARE_VERSION) + "/" + chipid);
  https.setReuse(false);
  https.begin(secClient, ATMOSUD_SERVER_URL);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("X-Sensor", String("esp32-") + chipid);

  unsigned long t0 = millis();
  int httpCode = https.POST(payload);
  float duration = (millis() - t0) / 1000.0;

  if (httpCode > 0) {
    Logger.printf("[AtmoSud] HTTP %d (%.1fs) - %s\n", httpCode, duration, https.getString().c_str());
    if (httpCode >= 200 && httpCode < 300) {
      Logger.println("[AtmoSud] Envoi reussi");
    } else {
      Logger.println("[AtmoSud] Reponse serveur non-OK");
    }
  } else {
    Logger.printf("[AtmoSud] Echec (%.1fs): %s\n", duration, https.errorToString(httpCode).c_str());
  }

  https.end();
}

SendResult dataSenderSend() {
  const SensorData& d = sensorsGetData();

  // Ne pas envoyer si aucune mesure n'a été faite
  if (d.lastReadTime == 0) {
    Logger.println("[AirCarto] Pas encore de donnees a envoyer");
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

  // Formaldéhyde (SFA40)
  if (d.sfa40_ok) {
    json += ",\"ISO_VB\":" + String(d.hcho, 1);
    json += ",\"ISO_VB_unit\":\"" + String(d.hcho, 1) + " ppb\"";
  }

  // error_flags (bitmask, cf. aircarto-protocols/formats/json-payload.md)
  // Le ModuleAir n'a que 3 bits applicables : BME280, NextPM, CO2.
  // Les bits 0/1 (RTC), 4 (ENVEA), 5 (NOISE), 6 (MPPT) restent a 0 car le
  // ModuleAir n'embarque pas ces capteurs — on ne doit PAS les detourner.
  // CCS811 (TVOC) et SFA40 (HCHO) n'ont pas de bit dedie dans le protocole :
  // leur absence est implicite (pas de champ ISO correspondant dans le JSON).
  //
  // Un capteur volontairement desactive par l'utilisateur (toggle web) ne
  // doit PAS lever son bit d'erreur : ce n'est pas une panne, c'est un choix.
  // On gate donc chaque bit sur (enabled && !ok).
  const SensorSettings& cfg = settingsGetSensors();
  uint8_t errorFlags = 0;
  if (cfg.bme280_enabled && !d.bme_ok) errorFlags |= 0x04;  // Bit 2 = BME280_ERROR
  if (cfg.npm_enabled    && !d.pm_ok)  errorFlags |= 0x08;  // Bit 3 = NPM_ERROR
  if (cfg.mhz19_enabled  && !d.co2_ok) errorFlags |= 0x80;  // Bit 7 = CO2_ERROR (MH-Z19 sur ModuleAir)
  json += ",\"error_flags\":" + String(errorFlags);

  // npm_status (registre statut NextPM)
  json += ",\"npm_status\":" + String(d.npmStatus);

  // device_status (bitmask)
  uint8_t deviceStatus = 0;
  deviceStatus |= 0x02;  // Bit 1 = WIFI_CONNECTED (toujours vrai ici, on envoie que si connecté)
  if (millis() < 300000) deviceStatus |= 0x80;  // Bit 7 = BOOT_RECENT (uptime < 5 min)
  json += ",\"device_status\":" + String(deviceStatus);

  json += "}";

  Logger.println("[AirCarto] POST " + String(DATA_SERVER_URL));
  Logger.println("[AirCarto] " + json);

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
    Logger.printf("[AirCarto] HTTP %d (%.1fs) - %s\n", httpCode, duration, http.getString().c_str());
    String serverDate = http.header("Date");
    if (serverDate.length() > 0) {
      Logger.printf("[AirCarto] Server time: %s\n", serverDate.c_str());
    }
    if (httpCode >= 200 && httpCode < 300) {
      result = SEND_OK;
    }
  } else {
    Logger.printf("[AirCarto] Internet OK mais serveur indisponible (%.1fs): %s\n", duration, http.errorToString(httpCode).c_str());
  }

  http.end();

  if (result == SEND_OK) {
    Logger.println("[AirCarto] Envoi reussi");
  } else {
    Logger.println("[AirCarto] Echec envoi - serveur data.moduleair.fr injoignable");
  }

  // Envoi secondaire AtmoSud, si activé. C'est un réglage RUNTIME stocké en NVS
  // (défaut de 1er boot = variante de build, cf. ATMOSUD_ENABLED_DEFAULT) qui
  // survit à l'OTA — d'où le test à l'exécution plutôt qu'un #ifdef. Indépendant
  // du résultat AirCarto ; l'accès internet a déjà été vérifié plus haut.
  if (settingsGetAtmosudEnabled()) {
    atmosudSend(d);
  }

  Logger.println();
  Logger.println("════════════════════════════════════════");
  Logger.printf("  Prochain envoi dans %ds\n", DATA_SEND_INTERVAL / 1000);
  Logger.println("════════════════════════════════════════");
  Logger.println();

  return result;
}
