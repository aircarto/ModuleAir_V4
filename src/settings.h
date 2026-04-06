#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

// Sensor enable/disable
struct SensorSettings {
  bool npm_enabled;
  bool mhz19_enabled;
  bool bme280_enabled;
  bool ccs811_enabled;
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
  // Logos
  bool logo_moduleair;
  bool logo_aircarto;
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

#endif
