#include <Arduino.h>
#include <Preferences.h>
#include "settings.h"
#include "config.h"
#include "logger.h"

static SensorSettings sensors;
static ScreenSettings screens;
static ThresholdsCO2 thCO2;

// Force a sensor's runtime-enabled flag to false when its compile-time
// master switch is 0. Called from settingsInit() AND settingsGetSensors()
// (the latter is defensive — even if NVS gets out of sync somehow, the
// effective flag reflects the build choice).
static inline void applyCompileTimeMask() {
#if !SENSOR_NPM_COMPILED
  sensors.npm_enabled = false;
#endif
#if !SENSOR_MHZ19_COMPILED
  sensors.mhz19_enabled = false;
#endif
#if !SENSOR_BME280_COMPILED
  sensors.bme280_enabled = false;
#endif
#if !SENSOR_CCS811_COMPILED
  sensors.ccs811_enabled = false;
#endif
#if !SENSOR_SFA40_COMPILED
  sensors.sfa40_enabled = false;
#endif
}

void settingsInit() {
  Preferences prefs;

  prefs.begin("sensors", true);
  sensors.npm_enabled    = prefs.getBool("npm", true);
  sensors.mhz19_enabled  = prefs.getBool("mhz19", true);
  sensors.bme280_enabled = prefs.getBool("bme280", true);
  sensors.ccs811_enabled = prefs.getBool("ccs811", true);
  sensors.sfa40_enabled  = prefs.getBool("sfa40", true);
  prefs.end();
  applyCompileTimeMask();

  prefs.begin("screens", true);
  screens.pm1  = prefs.getBool("pm1", true);
  screens.pm25 = prefs.getBool("pm25", true);
  screens.pm10 = prefs.getBool("pm10", true);
  screens.co2  = prefs.getBool("co2", true);
  screens.temp = prefs.getBool("temp", true);
  screens.humi = prefs.getBool("humi", true);
  screens.tvoc = prefs.getBool("tvoc", true);
  screens.hcho = prefs.getBool("hcho", true);
  screens.logo_moduleair = prefs.getBool("logo_ma", true);
  screens.logo_aircarto  = prefs.getBool("logo_ac", true);
  screens.logo_atmosud   = prefs.getBool("logo_as", true);
  prefs.end();

  prefs.begin("thresholds", true);
  thCO2.good = prefs.getInt("co2_good", 800);
  thCO2.bad  = prefs.getInt("co2_bad", 1500);
  prefs.end();

  Logger.printf("[Settings] Thresholds CO2: good<%d, bad>=%d\n", thCO2.good, thCO2.bad);
  Logger.printf("[Settings] Sensors: NPM=%d MHZ19=%d BME280=%d CCS811=%d SFA40=%d\n",
    sensors.npm_enabled, sensors.mhz19_enabled, sensors.bme280_enabled, sensors.ccs811_enabled, sensors.sfa40_enabled);
  Logger.printf("[Settings] Screens: PM1=%d PM2.5=%d PM10=%d CO2=%d Temp=%d Humi=%d COV=%d HCHO=%d Logo=%d AirCarto=%d AtmoSud=%d\n",
    screens.pm1, screens.pm25, screens.pm10, screens.co2, screens.temp, screens.humi, screens.tvoc, screens.hcho,
    screens.logo_moduleair, screens.logo_aircarto, screens.logo_atmosud);
}

SensorSettings& settingsGetSensors() {
  applyCompileTimeMask();   // defensive: stay consistent if NVS is stale
  return sensors;
}
ScreenSettings& settingsGetScreens() { return screens; }

// Hard-block re-enabling a sensor that's been compiled out. Three layers
// of defense for this case: the web UI greys the toggle and adds
// `disabled` so the browser refuses the click; this setter refuses to
// write NVS if a curl request gets through; settingsGetSensors() masks
// the runtime flag to false regardless of what NVS holds. The user's
// build-time choice is authoritative end-to-end.
static bool isSensorCompiledIn(const char* key) {
  if (!key) return true;
  if (strcmp(key, "npm")    == 0) return SENSOR_NPM_COMPILED    != 0;
  if (strcmp(key, "mhz19")  == 0) return SENSOR_MHZ19_COMPILED  != 0;
  if (strcmp(key, "bme280") == 0) return SENSOR_BME280_COMPILED != 0;
  if (strcmp(key, "ccs811") == 0) return SENSOR_CCS811_COMPILED != 0;
  if (strcmp(key, "sfa40")  == 0) return SENSOR_SFA40_COMPILED  != 0;
  return true;   // unknown keys: let through (forward-compat with future sensors)
}

void settingsSetSensorEnabled(const char* key, bool enabled) {
  if (enabled && !isSensorCompiledIn(key)) {
    Logger.printf("[Settings] Refused to enable '%s': disabled in code (SENSOR_%s_COMPILED=0)\n",
                  key, key);
    return;
  }

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
  sensors.sfa40_enabled  = prefs.getBool("sfa40", true);
  prefs.end();
  applyCompileTimeMask();
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
  screens.hcho = prefs.getBool("hcho", true);
  screens.logo_moduleair = prefs.getBool("logo_ma", true);
  screens.logo_aircarto  = prefs.getBool("logo_ac", true);
  screens.logo_atmosud   = prefs.getBool("logo_as", true);
  prefs.end();
}

ThresholdsCO2& settingsGetThresholdsCO2() { return thCO2; }

void settingsSetThresholdsCO2(int good, int bad) {
  if (good >= bad) return;  // sanity check
  thCO2.good = good;
  thCO2.bad = bad;
  Preferences prefs;
  prefs.begin("thresholds", false);
  prefs.putInt("co2_good", good);
  prefs.putInt("co2_bad", bad);
  prefs.end();
  Logger.printf("[Settings] Thresholds CO2 updated: good<%d, bad>=%d\n", good, bad);
}
