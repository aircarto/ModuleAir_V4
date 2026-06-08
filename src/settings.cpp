#include <Arduino.h>
#include <Preferences.h>
#include "settings.h"
#include "config.h"
#include "logger.h"

static SensorSettings sensors;
static ScreenSettings screens;
static ThresholdsCO2 thCO2;
static bool atmosudEnabled;

void settingsInit() {
  Preferences prefs;

  // The SENSOR_*_DEFAULT macros (config.h) are only the first-boot defaults
  // for getBool(): once a value is stored in NVS (user toggled it in the UI),
  // that stored value wins. So disabling a sensor in code just makes it start
  // off — the user can still re-enable it at runtime.
  prefs.begin("sensors", true);
  sensors.npm_enabled    = prefs.getBool("npm",    SENSOR_NPM_DEFAULT);
  sensors.mhz19_enabled  = prefs.getBool("mhz19",  SENSOR_MHZ19_DEFAULT);
  sensors.bme280_enabled = prefs.getBool("bme280", SENSOR_BME280_DEFAULT);
  sensors.ccs811_enabled = prefs.getBool("ccs811", SENSOR_CCS811_DEFAULT);
  sensors.sfa40_enabled  = prefs.getBool("sfa40",  SENSOR_SFA40_DEFAULT);
  prefs.end();

  prefs.begin("screens", true);
  screens.pm1  = prefs.getBool("pm1",  SCREEN_PM1_DEFAULT);
  screens.pm25 = prefs.getBool("pm25", SCREEN_PM25_DEFAULT);
  screens.pm10 = prefs.getBool("pm10", SCREEN_PM10_DEFAULT);
  screens.co2  = prefs.getBool("co2",  SCREEN_CO2_DEFAULT);
  screens.temp = prefs.getBool("temp", SCREEN_TEMP_DEFAULT);
  screens.humi = prefs.getBool("humi", SCREEN_HUMI_DEFAULT);
  screens.tvoc = prefs.getBool("tvoc", SCREEN_TVOC_DEFAULT);
  screens.hcho = prefs.getBool("hcho", SCREEN_HCHO_DEFAULT);
  screens.logo_moduleair = prefs.getBool("logo_ma", LOGO_MODULEAIR_DEFAULT);
  screens.logo_aircarto  = prefs.getBool("logo_ac", LOGO_AIRCARTO_DEFAULT);
  screens.logo_atmosud   = prefs.getBool("logo_as", LOGO_ATMOSUD_DEFAULT);
#ifdef BUILD_LAIRETMOI
  screens.logo_lairetmoi = prefs.getBool("logo_lam", LOGO_LAIRETMOI_DEFAULT);
#endif
  prefs.end();

  prefs.begin("thresholds", true);
  thCO2.good = prefs.getInt("co2_good", 800);
  thCO2.bad  = prefs.getInt("co2_bad", 1500);
  prefs.end();

  // AtmoSud secondary server: runtime flag stored in NVS. Unlike the sensor
  // defaults above, this default is BUILD-VARIANT-DEPENDENT (ATMOSUD_ENABLED_DEFAULT
  // follows -DBUILD_ATMOSUD), so we must BAKE it into NVS on first boot — exactly
  // like the language default in i18nInit. Otherwise an OTA to the classic binary
  // (whose compile default is false) would silently stop AtmoSud sending on a
  // board that was flashed as an AtmoSud unit. Once written, the stored value
  // wins on every later boot (until the user toggles it in the web UI) and
  // survives every OTA.
  prefs.begin("server", false);
  uint8_t as = prefs.getUChar("atmosud", 0xFF);   // 0xFF = key absent
  if (as > 1) {
    as = ATMOSUD_ENABLED_DEFAULT ? 1 : 0;
    prefs.putUChar("atmosud", as);
  }
  atmosudEnabled = (as == 1);
  prefs.end();

  Logger.printf("[Settings] Thresholds CO2: good<%d, bad>=%d\n", thCO2.good, thCO2.bad);
  Logger.println("[Settings] ── Envoi des donnees ──");
  Logger.println("[Settings]   -> AirCarto (data.moduleair.fr) : OUI (toujours actif)");
  Logger.printf ("[Settings]   -> AtmoSud  (uspot.probesys.net) : %s (defaut build : %s)\n",
    atmosudEnabled ? "OUI" : "NON", ATMOSUD_ENABLED_DEFAULT ? "OUI" : "NON");
  Logger.printf("[Settings] Sensors: NPM=%d MHZ19=%d BME280=%d CCS811=%d SFA40=%d\n",
    sensors.npm_enabled, sensors.mhz19_enabled, sensors.bme280_enabled, sensors.ccs811_enabled, sensors.sfa40_enabled);
  Logger.printf("[Settings] Screens: PM1=%d PM2.5=%d PM10=%d CO2=%d Temp=%d Humi=%d COV=%d HCHO=%d Logo=%d AirCarto=%d AtmoSud=%d\n",
    screens.pm1, screens.pm25, screens.pm10, screens.co2, screens.temp, screens.humi, screens.tvoc, screens.hcho,
    screens.logo_moduleair, screens.logo_aircarto, screens.logo_atmosud);
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
  sensors.npm_enabled    = prefs.getBool("npm",    SENSOR_NPM_DEFAULT);
  sensors.mhz19_enabled  = prefs.getBool("mhz19",  SENSOR_MHZ19_DEFAULT);
  sensors.bme280_enabled = prefs.getBool("bme280", SENSOR_BME280_DEFAULT);
  sensors.ccs811_enabled = prefs.getBool("ccs811", SENSOR_CCS811_DEFAULT);
  sensors.sfa40_enabled  = prefs.getBool("sfa40",  SENSOR_SFA40_DEFAULT);
  prefs.end();
}

void settingsSetScreenEnabled(const char* key, bool enabled) {
  Preferences prefs;
  prefs.begin("screens", false);
  prefs.putBool(key, enabled);
  prefs.end();

  prefs.begin("screens", true);
  screens.pm1  = prefs.getBool("pm1",  SCREEN_PM1_DEFAULT);
  screens.pm25 = prefs.getBool("pm25", SCREEN_PM25_DEFAULT);
  screens.pm10 = prefs.getBool("pm10", SCREEN_PM10_DEFAULT);
  screens.co2  = prefs.getBool("co2",  SCREEN_CO2_DEFAULT);
  screens.temp = prefs.getBool("temp", SCREEN_TEMP_DEFAULT);
  screens.humi = prefs.getBool("humi", SCREEN_HUMI_DEFAULT);
  screens.tvoc = prefs.getBool("tvoc", SCREEN_TVOC_DEFAULT);
  screens.hcho = prefs.getBool("hcho", SCREEN_HCHO_DEFAULT);
  screens.logo_moduleair = prefs.getBool("logo_ma", LOGO_MODULEAIR_DEFAULT);
  screens.logo_aircarto  = prefs.getBool("logo_ac", LOGO_AIRCARTO_DEFAULT);
  screens.logo_atmosud   = prefs.getBool("logo_as", LOGO_ATMOSUD_DEFAULT);
#ifdef BUILD_LAIRETMOI
  screens.logo_lairetmoi = prefs.getBool("logo_lam", LOGO_LAIRETMOI_DEFAULT);
#endif
  prefs.end();
}

bool settingsGetAtmosudEnabled() { return atmosudEnabled; }

void settingsSetAtmosudEnabled(bool enabled) {
  atmosudEnabled = enabled;
  Preferences prefs;
  prefs.begin("server", false);
  prefs.putUChar("atmosud", enabled ? 1 : 0);
  prefs.end();
  Logger.printf("[Settings] AtmoSud server %s\n", enabled ? "ENABLED" : "disabled");
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
