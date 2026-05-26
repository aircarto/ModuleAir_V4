#include <Arduino.h>
#include <Wire.h>
#include <ModbusMaster.h>
#include <MHZ19.h>
#include <Adafruit_BME280.h>
#include <Adafruit_CCS811.h>
#include "config.h"
#include "sensors.h"
#include "settings.h"
#include "logger.h"

static HardwareSerial nextpmSerial(1);
static ModbusMaster nextpm;

static HardwareSerial mhzSerial(2);
static MHZ19 mhz19;

// Previous toggle state, used to detect disabled->enabled transitions in
// sensorsRead() so we can drain the stale RX buffer and re-bind the library
// to the (already opened) UART. We follow the ESPHome / Tasmota convention:
// keep the UART permanently begun at boot regardless of the toggle, and gate
// the actual read at poll time. This avoids HardwareSerial::end()/begin()
// re-init bugs that arduino-esp32 has accumulated over the years.
static bool prevNpmEnabled = false;
static bool prevMhzEnabled = false;

static Adafruit_BME280 bme;
static bool bmeFound = false;

static Adafruit_CCS811 ccs;
static bool ccsFound = false;

// SFA40 formaldehyde sensor (I2C, raw driver)
#define SFA40_ADDR 0x5D
static bool sfa40Found = false;
static bool sfa40Started = false;

static SensorData data = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, false, false, false, false, false, 0};

// Registres NextPM
#define REG_STATUS     0x13
#define REG_PM1_1MIN   0x44
#define REG_PM25_1MIN  0x46
#define REG_PM10_1MIN  0x48
#define REG_TEMP_INT   0x6B
#define REG_HUM_INT    0x6A

// Bits du registre status NextPM — convention serveur AirCarto
// (cf. aircarto-protocols/formats/json-payload.md § npm_status)
#define NPM_STATUS_SLEEP        0x01
#define NPM_STATUS_DEGRADED     0x02
#define NPM_STATUS_NOT_READY    0x04
#define NPM_STATUS_HEAT_ERROR   0x08
#define NPM_STATUS_TRH_ERROR    0x10
#define NPM_STATUS_FAN_ERROR    0x20
#define NPM_STATUS_MEMORY_ERROR 0x40
#define NPM_STATUS_LASER_ERROR  0x80

// Bits qui invalident une mesure PM (laser HS, ventilo HS, heater, pas pret)
#define NPM_STATUS_CRITICAL (NPM_STATUS_NOT_READY | NPM_STATUS_HEAT_ERROR | \
                             NPM_STATUS_FAN_ERROR | NPM_STATUS_LASER_ERROR)

// Plage de mesure NextPM (datasheet Tera Sensor : 0-1000 µg/m³)
#define NPM_PM_MAX 1000.0f

// Plage de mesure MH-Z19B / MH-Z16 (datasheet Winsen : 0-5000 ppm en air ambiant)
// 400 ppm ≈ CO2 atmospherique de fond — en dessous c'est physiquement impossible
#define MHZ_CO2_MIN 400
#define MHZ_CO2_MAX 5000

// Ping I2C : renvoie true si un device repond a cette adresse.
// Utilise pour detecter les capteurs branches/debranches en cours d'utilisation
// (au lieu de figer leur etat au boot).
static bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Stocke l'adresse I2C effective du BME280 (0x76 ou 0x77) une fois trouvee
static uint8_t bmeAddr = 0;

static uint32_t readU32(uint16_t regLow) {
  uint8_t result = nextpm.readHoldingRegisters(regLow, 2);
  if (result == nextpm.ku8MBSuccess) {
    uint16_t lowWord  = nextpm.getResponseBuffer(0);
    uint16_t highWord = nextpm.getResponseBuffer(1);
    return ((uint32_t)highWord << 16) | lowWord;
  }
  Logger.printf("  Modbus error reg 0x%02X: 0x%02X\n", regLow, result);
  return 0xFFFFFFFF;
}

static uint16_t readU16(uint16_t reg) {
  uint8_t result = nextpm.readHoldingRegisters(reg, 1);
  if (result == nextpm.ku8MBSuccess) {
    return nextpm.getResponseBuffer(0);
  }
  Logger.printf("  Modbus error reg 0x%02X: 0x%02X\n", reg, result);
  return 0xFFFF;
}

static void readNextPM() {
  Logger.println("[NextPM] Reading...");

  // 1) Lire le registre status — distinguer echec Modbus (0xFF) d'un status nominal (0x00)
  uint8_t statusResult = nextpm.readHoldingRegisters(REG_STATUS, 1);
  if (statusResult != nextpm.ku8MBSuccess) {
    data.npmStatus = 0xFF;  // sentinelle "indisponible" du protocole serveur
    data.pm_ok = false;
    Logger.printf("  Modbus error sur status: 0x%02X — capteur muet\n", statusResult);
    Logger.println();
    return;
  }
  data.npmStatus = (uint8_t)(nextpm.getResponseBuffer(0) & 0xFF);
  Logger.printf("  Status: 0x%02X\n", data.npmStatus);

  // 2) Si un bit critique est leve, la mesure n'est pas fiable
  if (data.npmStatus & NPM_STATUS_CRITICAL) {
    if (data.npmStatus & NPM_STATUS_LASER_ERROR) Logger.println("    -> LASER_ERROR (>240s sans particule)");
    if (data.npmStatus & NPM_STATUS_FAN_ERROR)   Logger.println("    -> FAN_ERROR (ventilateur hors plage)");
    if (data.npmStatus & NPM_STATUS_HEAT_ERROR)  Logger.println("    -> HEAT_ERROR (humidite >60%% >10min)");
    if (data.npmStatus & NPM_STATUS_NOT_READY)   Logger.println("    -> NOT_READY (capteur pas pret)");
    data.pm_ok = false;
    Logger.println();
    return;
  }

  // 3) Lire les PM
  uint32_t pm1_raw  = readU32(REG_PM1_1MIN);
  uint32_t pm25_raw = readU32(REG_PM25_1MIN);
  uint32_t pm10_raw = readU32(REG_PM10_1MIN);

  if (pm1_raw == 0xFFFFFFFF || pm25_raw == 0xFFFFFFFF || pm10_raw == 0xFFFFFFFF) {
    data.pm_ok = false;
    Logger.println("  Erreur Modbus sur lecture PM");
    Logger.println();
    return;
  }

  float pm1  = pm1_raw  / 1000.0f;
  float pm25 = pm25_raw / 1000.0f;
  float pm10 = pm10_raw / 1000.0f;

  // 4) Filtrer les valeurs hors plage capteur (0-1000 µg/m³)
  if (pm1 > NPM_PM_MAX || pm25 > NPM_PM_MAX || pm10 > NPM_PM_MAX) {
    data.pm_ok = false;
    Logger.printf("  Valeurs aberrantes: PM1=%.1f PM2.5=%.1f PM10=%.1f (max %.0f ug/m3)\n",
                  pm1, pm25, pm10, NPM_PM_MAX);
    Logger.println();
    return;
  }

  data.pm1  = pm1;
  data.pm25 = pm25;
  data.pm10 = pm10;
  data.pm_ok = true;
  Logger.printf("  PM1.0:  %.3f ug/m3\n", data.pm1);
  Logger.printf("  PM2.5:  %.3f ug/m3\n", data.pm25);
  Logger.printf("  PM10:   %.3f ug/m3\n", data.pm10);

  // Temp/hum internes du capteur — purement informatif (la temp/hum metier vient du BME280)
  uint16_t temp_raw = readU16(REG_TEMP_INT);
  uint16_t hum_raw  = readU16(REG_HUM_INT);
  if (temp_raw != 0xFFFF)
    Logger.printf("  Temp (interne): %.1f C\n", temp_raw / 100.0);
  if (hum_raw != 0xFFFF)
    Logger.printf("  Hum (interne):  %.1f %%\n", hum_raw / 100.0);

  Logger.println();
}

static void readMHZ19() {
  Logger.println("[MH-Z19] Reading...");

  int co2 = mhz19.getCO2();
  uint8_t err = mhz19.errorCode;

  // 1) Verifier l'errorCode de la lib WifWaf
  if (err != RESULT_OK) {
    data.co2_ok = false;
    switch (err) {
      case RESULT_NULL:    Logger.println("  Pas de reponse (NULL)"); break;
      case RESULT_TIMEOUT: Logger.println("  Timeout UART — capteur deconnecte ?"); break;
      case RESULT_MATCH:   Logger.println("  Header de trame invalide"); break;
      case RESULT_CRC:     Logger.println("  Checksum invalide (bruit ligne ?)"); break;
      case RESULT_FILTER:  Logger.println("  Valeur filtree par la lib (warm-up detecte)"); break;
      default:             Logger.printf("  Erreur lib MHZ19: %u\n", err); break;
    }
    Logger.println();
    return;
  }

  // 2) Filtrer les valeurs hors plage physique (400-5000 ppm)
  if (co2 < MHZ_CO2_MIN || co2 > MHZ_CO2_MAX) {
    data.co2_ok = false;
    Logger.printf("  CO2 hors plage: %d ppm (attendu %d-%d)\n", co2, MHZ_CO2_MIN, MHZ_CO2_MAX);
    Logger.println();
    return;
  }

  data.co2 = co2;
  data.co2_ok = true;
  Logger.printf("  CO2:  %d ppm\n", co2);
  Logger.printf("  Temp (MH-Z19): %d C\n", mhz19.getTemperature());
  Logger.println();
}

static void readBME280() {
  Logger.println("[BME280] Reading...");

  // 1) Ping I2C aux 2 adresses possibles
  uint8_t presentAddr = 0;
  if      (i2cProbe(0x76)) presentAddr = 0x76;
  else if (i2cProbe(0x77)) presentAddr = 0x77;

  if (presentAddr == 0) {
    if (bmeFound) Logger.println("  Capteur perdu (etait present, debranche ?)");
    else          Logger.println("  Capteur absent");
    bmeFound = false;
    bmeAddr = 0;
    data.bme_ok = false;
    Logger.println();
    return;
  }

  // 2) Apparu ou re-apparu : initialiser
  if (!bmeFound || presentAddr != bmeAddr) {
    Logger.printf("  Capteur (re)detecte a 0x%02X, initialisation...\n", presentAddr);
    if (bme.begin(presentAddr)) {
      bmeFound = true;
      bmeAddr = presentAddr;
      bme.setSampling(Adafruit_BME280::MODE_FORCED,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::FILTER_OFF);
      Logger.println("  Init OK");
    } else {
      Logger.println("  Init echoue malgre I2C present");
      data.bme_ok = false;
      Logger.println();
      return;
    }
  }

  // 3) Lecture normale
  float temperature = bme.readTemperature();
  float humidity    = bme.readHumidity();
  float pressure    = bme.readPressure() / 100.0;

  if (isnan(temperature) || isnan(pressure)) {
    data.bme_ok = false;
    Logger.println("  Erreur de lecture (NaN)");
    Logger.println();
    return;
  }

  data.temperature = temperature;
  data.humidity    = humidity;
  data.pressure    = pressure;
  data.bme_ok = true;

  Logger.printf("  Temp:     %.1f C\n", temperature);
  Logger.printf("  Humidity: %.1f %%\n", humidity);
  Logger.printf("  Pressure: %.1f hPa\n", pressure);
  Logger.println();
}

static void readCCS811() {
  Logger.println("[CCS811] Reading...");

  // 1) Ping I2C (CCS811 = adresse 0x5A par defaut, 0x5B en option)
  bool present = i2cProbe(0x5A) || i2cProbe(0x5B);

  if (!present) {
    if (ccsFound) Logger.println("  Capteur perdu (etait present, debranche ?)");
    else          Logger.println("  Capteur absent");
    ccsFound = false;
    data.ccs_ok = false;
    Logger.println();
    return;
  }

  // 2) Apparu : initialiser
  if (!ccsFound) {
    Logger.println("  Capteur (re)detecte, initialisation...");
    if (ccs.begin()) {
      ccsFound = true;
      ccs.setDriveMode(CCS811_DRIVE_MODE_10SEC);
      Logger.println("  Init OK");
    } else {
      Logger.println("  Init echoue malgre I2C present");
      data.ccs_ok = false;
      Logger.println();
      return;
    }
  }

  // 3) Lecture normale
  if (!ccs.available()) {
    data.ccs_ok = false;
    Logger.println("  Donnees pas encore pretes (warm-up)");
    Logger.println();
    return;
  }

  // Compensation température/humidité depuis le BME280
  if (data.bme_ok) {
    ccs.setEnvironmentalData(data.humidity, data.temperature);
  }

  if (ccs.readData() == 0) {
    data.tvoc = ccs.getTVOC();
    data.eco2 = ccs.geteCO2();
    data.ccs_ok = true;
    Logger.printf("  TVOC:  %d ppb\n", data.tvoc);
    Logger.printf("  eCO2:  %d ppm\n", data.eco2);
  } else {
    data.ccs_ok = false;
    Logger.println("  Erreur de lecture");
  }

  Logger.println();
}

// ── SFA40 helpers ──

static uint8_t sfa40Crc(uint8_t d0, uint8_t d1) {
  uint8_t crc = 0xFF;
  uint8_t bytes[] = {d0, d1};
  for (int i = 0; i < 2; i++) {
    crc ^= bytes[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
    }
  }
  return crc;
}

static bool sfa40SendCmd(uint16_t cmd) {
  Wire.beginTransmission(SFA40_ADDR);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)(cmd & 0xFF));
  return Wire.endTransmission() == 0;
}

static void readSFA40() {
  Logger.println("[SFA40] Reading...");

  // 1) Ping I2C
  bool present = i2cProbe(SFA40_ADDR);

  if (!present) {
    if (sfa40Found) Logger.println("  Capteur perdu (etait present, debranche ?)");
    else            Logger.println("  Capteur absent");
    sfa40Found = false;
    sfa40Started = false;
    data.sfa40_ok = false;
    Logger.println();
    return;
  }

  // 2) Apparu : initialiser et lancer la mesure
  if (!sfa40Found) {
    Logger.println("  Capteur (re)detecte, initialisation...");
    sfa40SendCmd(0x50D2);  // Stop (reset to known state)
    delay(50);
    sfa40SendCmd(0x00AC);  // Start Measurement
    sfa40Started = true;
    sfa40Found = true;
    Logger.println("  Init OK, mesure demarree (donnees au prochain cycle)");
    Logger.println();
    return;
  }

  // Start measurement if not yet started
  if (!sfa40Started) {
    sfa40SendCmd(0x00AC);  // Start Measurement
    sfa40Started = true;
    Logger.println("  Measurement started, waiting for next cycle");
    Logger.println();
    return;
  }

  // Read measurement data (command 0xE06D, returns 12 bytes)
  if (!sfa40SendCmd(0xE06D)) {
    data.sfa40_ok = false;
    Logger.println("  I2C command failed");
    Logger.println();
    return;
  }

  delay(5);  // sensor needs a short delay before data is ready

  uint8_t buf[12];
  Wire.requestFrom((uint8_t)SFA40_ADDR, (uint8_t)12);
  if (Wire.available() < 12) {
    data.sfa40_ok = false;
    Logger.println("  Not enough data received");
    Logger.println();
    return;
  }
  for (int i = 0; i < 12; i++) buf[i] = Wire.read();

  // Check status byte (byte 10): 0 = data ready
  uint8_t status = buf[10];
  if (status != 0) {
    data.sfa40_ok = false;
    if (status & 0x01) Logger.println("  Sensor warming up...");
    else Logger.printf("  Sensor status: 0x%02X\n", status);
    Logger.println();
    return;
  }

  // Verify CRC for each 2-byte group
  if (sfa40Crc(buf[0], buf[1]) != buf[2] ||
      sfa40Crc(buf[3], buf[4]) != buf[5] ||
      sfa40Crc(buf[6], buf[7]) != buf[8]) {
    data.sfa40_ok = false;
    Logger.println("  CRC error");
    Logger.println();
    return;
  }

  // Parse values
  uint16_t hchoRaw = ((uint16_t)buf[0] << 8) | buf[1];
  data.hcho = hchoRaw / 10.0;
  data.sfa40_ok = true;

  // Humidity and temperature from SFA40 (for logging, not stored — BME280 is primary)
  uint16_t humRaw  = ((uint16_t)buf[3] << 8) | buf[4];
  uint16_t tempRaw = ((uint16_t)buf[6] << 8) | buf[7];
  float sfa40Hum  = 125.0 * (humRaw / 65535.0) - 6.0;
  float sfa40Temp = 175.0 * (tempRaw / 65535.0) - 45.0;

  Logger.printf("  HCHO:     %.1f ppb\n", data.hcho);
  Logger.printf("  Temp (SFA40): %.1f C\n", sfa40Temp);
  Logger.printf("  Hum  (SFA40): %.1f %%\n", sfa40Hum);
  Logger.println();
}

void sensorsInit() {
  const SensorSettings& cfg = settingsGetSensors();

  // UART sensors: always open the port at boot regardless of the user toggle.
  // The toggle gates the *read*, not the hardware lifecycle. This means a
  // user re-enabling NPM or MH-Z19 from the web UI gets a working sensor on
  // the next cycle without needing a reboot, and we never call
  // HardwareSerial::end() (which has a long bug history on ESP32).
  nextpmSerial.begin(NEXTPM_BAUD, SERIAL_8E1, NEXTPM_RX, NEXTPM_TX);
  nextpm.begin(NEXTPM_ADDR, nextpmSerial);
  Logger.println(cfg.npm_enabled ? "NextPM init OK (Modbus RTU)"
                                 : "NextPM init OK (Modbus RTU, disabled by user)");
  prevNpmEnabled = cfg.npm_enabled;

  mhzSerial.begin(MHZ19_BAUD, SERIAL_8N1, MHZ19_RX, MHZ19_TX);
  mhz19.begin(mhzSerial);
  mhz19.autoCalibration(false);
  Logger.println(cfg.mhz19_enabled ? "MH-Z19 init OK"
                                   : "MH-Z19 init OK (disabled by user)");
  prevMhzEnabled = cfg.mhz19_enabled;

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  if (cfg.bme280_enabled) {
    // Tentative de detection au boot — sera reverifiee a chaque cycle de read
    if (bme.begin(0x76)) {
      bmeFound = true;
      bmeAddr = 0x76;
      bme.setSampling(Adafruit_BME280::MODE_FORCED,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::FILTER_OFF);
      Logger.println("BME280 init OK (0x76)");
    } else if (bme.begin(0x77)) {
      bmeFound = true;
      bmeAddr = 0x77;
      bme.setSampling(Adafruit_BME280::MODE_FORCED,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::FILTER_OFF);
      Logger.println("BME280 init OK (0x77)");
    } else {
      Logger.println("BME280 not found au boot — sera reverifie chaque cycle");
    }
  } else {
    Logger.println("BME280 disabled");
  }

  if (cfg.ccs811_enabled) {
    if (ccs.begin()) {
      ccsFound = true;
      ccs.setDriveMode(CCS811_DRIVE_MODE_10SEC);
      Logger.println("CCS811 init OK (0x5A)");
    } else {
      Logger.println("CCS811 not found au boot — sera reverifie chaque cycle");
    }
  } else {
    Logger.println("CCS811 disabled");
  }

  if (cfg.sfa40_enabled) {
    if (i2cProbe(SFA40_ADDR)) {
      sfa40Found = true;
      sfa40SendCmd(0x50D2);  // Stop (reset to known state)
      delay(50);
      sfa40SendCmd(0x00AC);  // Start Measurement
      sfa40Started = true;
      Logger.println("SFA40 init OK (0x5D)");
    } else {
      Logger.println("SFA40 not found au boot — sera reverifie chaque cycle");
    }
  } else {
    Logger.println("SFA40 disabled");
  }
}

// On a disabled->enabled UART transition, the sensor has been streaming or
// idling on a line whose RX FIFO we haven't drained for an arbitrary amount
// of time. Read it dry, give it a brief settle window, and re-attach the
// library to the stream (calling begin() again is free — both libs just
// store a Stream* — but it resets any internal state machine).
static void rearmNpmUart() {
  while (nextpmSerial.available()) nextpmSerial.read();
  delay(80);
  nextpm.begin(NEXTPM_ADDR, nextpmSerial);
  Logger.println("[NextPM] Re-enabled at runtime");
}

static void rearmMhzUart() {
  while (mhzSerial.available()) mhzSerial.read();
  delay(80);
  mhz19.begin(mhzSerial);
  mhz19.autoCalibration(false);
  Logger.println("[MH-Z19] Re-enabled at runtime");
}

void sensorsRead() {
  const SensorSettings& cfg = settingsGetSensors();

  // When a sensor is disabled by the user, force its _ok flag to false so
  // downstream consumers (data_sender, display, web dashboard) stop treating
  // the last cached reading as fresh data. Without this, toggling a sensor
  // off only stops the read but leaves stale values being sent.
  if (cfg.npm_enabled) {
    if (!prevNpmEnabled) rearmNpmUart();
    readNextPM();
  } else {
    data.pm_ok = false;
  }
  prevNpmEnabled = cfg.npm_enabled;

  if (cfg.mhz19_enabled) {
    if (!prevMhzEnabled) rearmMhzUart();
    readMHZ19();
  } else {
    data.co2_ok = false;
  }
  prevMhzEnabled = cfg.mhz19_enabled;

  if (cfg.bme280_enabled) readBME280();  else data.bme_ok   = false;
  if (cfg.ccs811_enabled) readCCS811();  else data.ccs_ok   = false;
  if (cfg.sfa40_enabled)  readSFA40();   else data.sfa40_ok = false;
  data.lastReadTime = millis();
}

const SensorData& sensorsGetData() {
  return data;
}
