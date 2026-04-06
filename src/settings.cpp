#include <Arduino.h>
#include <Preferences.h>
#include "settings.h"
#include "logger.h"

static SensorSettings sensors;
static ScreenSettings screens;

void settingsInit() {
  Preferences prefs;

  prefs.begin("sensors", true);
  sensors.npm_enabled    = prefs.getBool("npm", true);
  sensors.mhz19_enabled  = prefs.getBool("mhz19", true);
  sensors.bme280_enabled = prefs.getBool("bme280", true);
  sensors.ccs811_enabled = prefs.getBool("ccs811", true);
  prefs.end();

  prefs.begin("screens", true);
  screens.pm1  = prefs.getBool("pm1", true);
  screens.pm25 = prefs.getBool("pm25", true);
  screens.pm10 = prefs.getBool("pm10", true);
  screens.co2  = prefs.getBool("co2", true);
  screens.temp = prefs.getBool("temp", true);
  screens.humi = prefs.getBool("humi", true);
  screens.tvoc = prefs.getBool("tvoc", true);
  prefs.end();

  Logger.printf("[Settings] Sensors: NPM=%d MHZ19=%d BME280=%d CCS811=%d\n",
    sensors.npm_enabled, sensors.mhz19_enabled, sensors.bme280_enabled, sensors.ccs811_enabled);
  Logger.printf("[Settings] Screens: PM1=%d PM2.5=%d PM10=%d CO2=%d Temp=%d Humi=%d COV=%d\n",
    screens.pm1, screens.pm25, screens.pm10, screens.co2, screens.temp, screens.humi, screens.tvoc);
}

SensorSettings& settingsGetSensors() { return sensors; }
ScreenSettings& settingsGetScreens() { return screens; }

void settingsSetSensorEnabled(const char* key, bool enabled) {
  Preferences prefs;
  prefs.begin("sensors", false);
  prefs.putBool(key, enabled);
  prefs.end();

  // Reload
  prefs.begin("sensors", true);
  sensors.npm_enabled    = prefs.getBool("npm", true);
  sensors.mhz19_enabled  = prefs.getBool("mhz19", true);
  sensors.bme280_enabled = prefs.getBool("bme280", true);
  sensors.ccs811_enabled = prefs.getBool("ccs811", true);
  prefs.end();
}

void settingsSetScreenEnabled(const char* key, bool enabled) {
  Preferences prefs;
  prefs.begin("screens", false);
  prefs.putBool(key, enabled);
  prefs.end();

  prefs.begin("screens", true);
  screens.pm1  = prefs.getBool("pm1", true);
  screens.pm25 = prefs.getBool("pm25", true);
  screens.pm10 = prefs.getBool("pm10", true);
  screens.co2  = prefs.getBool("co2", true);
  screens.temp = prefs.getBool("temp", true);
  screens.humi = prefs.getBool("humi", true);
  screens.tvoc = prefs.getBool("tvoc", true);
  prefs.end();
}
