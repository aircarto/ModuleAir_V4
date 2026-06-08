#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

// Sensor enable/disable
struct SensorSettings {
  bool npm_enabled;
  bool mhz19_enabled;
  bool bme280_enabled;
  bool ccs811_enabled;
  bool sfa40_enabled;
};

// Display screen enable/disable
struct ScreenSettings {
  // Polluants
  bool pm1;
  bool pm25;
  bool pm10;
  bool co2;
  bool temp;
  bool humi;
  bool tvoc;
  bool hcho;
  // Logos
  bool logo_moduleair;
  bool logo_aircarto;
  bool logo_atmosud;
#ifdef BUILD_LAIRETMOI
  bool logo_lairetmoi;  // build lairetmoi uniquement
#endif
};

void settingsInit();

SensorSettings& settingsGetSensors();
ScreenSettings& settingsGetScreens();

void settingsSetSensorEnabled(const char* key, bool enabled);
void settingsSetScreenEnabled(const char* key, bool enabled);

// Thresholds
struct ThresholdsCO2 {
  int good;    // below = green
  int bad;     // above = red, between = orange
};

ThresholdsCO2& settingsGetThresholdsCO2();
void settingsSetThresholdsCO2(int good, int bad);

// ── Serveurs de données ─────────────────────────────────────────────────────
// AirCarto (DATA_SERVER_URL) est TOUJOURS actif. AtmoSud est un envoi secondaire
// optionnel : flag runtime stocké en NVS, dont le défaut de premier boot suit la
// variante de build (ATMOSUD_ENABLED_DEFAULT). Persiste à travers l'OTA, comme
// tous les autres réglages.
bool settingsGetAtmosudEnabled();
void settingsSetAtmosudEnabled(bool enabled);

#endif
