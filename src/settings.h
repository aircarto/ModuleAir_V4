#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

// Sensor enable/disable
//
// NB : il n'y a AUCUN réglage de modèle de capteur CO2 ici. MH-Z19 et SenseAir
// S8/S88 sont détectés automatiquement au runtime (sensors.cpp) ; `mhz19_enabled`
// n'active/coupe que la VOIE CO2, quel que soit le modèle branché.
struct SensorSettings {
  bool npm_enabled;
  bool mhz19_enabled;   // active/désactive la voie CO2 (quel que soit le capteur)
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
// AirCarto (DATA_SERVER_URL) est TOUJOURS actif. L'envoi secondaire AtmoSud
// n'est PLUS un réglage : c'est une propriété intrinsèque du capteur (stamp NVS
// write-once posé à l'usine) — voir data_sender.h (dataSenderIsAtmosudDevice)
// et config.h pour l'architecture.

#endif
