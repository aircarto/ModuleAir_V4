#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

// Quel capteur CO2 est monté sur la carte. C'est un CHOIX utilisateur/usine,
// à ne pas confondre avec le résultat de détection interne à sensors.cpp
// (enum Co2Sensor) : ici on dit quoi chercher, là-bas on dit ce qui a répondu.
// Les valeurs sont persistées telles quelles en NVS — NE PAS renuméroter.
enum Co2SensorChoice : uint8_t {
  CO2_CHOICE_AUTO  = 0,   // sonde MH-Z19 puis SenseAir, mémorise le gagnant
  CO2_CHOICE_MHZ19 = 1,   // force le protocole MH-Z19
  CO2_CHOICE_S88   = 2,   // force le Modbus RTU SenseAir S8/S88
};

// Sensor enable/disable
struct SensorSettings {
  bool npm_enabled;
  bool mhz19_enabled;   // active/désactive la voie CO2 (quel que soit le capteur)
  bool bme280_enabled;
  bool ccs811_enabled;
  bool sfa40_enabled;
  Co2SensorChoice co2_sensor;  // quel capteur CO2 sur la voie ci-dessus
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
void settingsSetCo2Sensor(Co2SensorChoice choice);
const char* settingsCo2SensorLabel(Co2SensorChoice choice);  // libellé de log (non traduit)

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
