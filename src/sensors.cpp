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
// sensorsSample()/sensorsFinalize() so we can drain the stale RX buffer and re-bind the library
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

// `data` = struct de TRAVAIL : chaque readXxx() y écrit sa dernière lecture
// instantanée. Ce n'est PAS ce qui est publié au reste du firmware.
static SensorData data = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, false, false, false, false, false, 0, SENSOR_ABSENT};

// `published` = snapshot PUBLIÉ (renvoyé par sensorsGetData(), lu par le
// display, le data_sender et le dashboard web). Rempli uniquement par
// sensorsFinalize() avec les moyennes de la fenêtre + l'état PM courant.
static SensorData published = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, false, false, false, false, false, 0, SENSOR_ABSENT};

// Accumulateurs de la fenêtre d'envoi : somme + nombre d'échantillons VALIDES
// par capteur "spot". À l'envoi, moyenne = somme / nombre (cf. sensorsFinalize).
// Sommes en double pour éviter tout overflow et garder la précision.
struct SensorAccum {
  double co2;    uint16_t co2_n;
  double temp;
  double hum;
  double press;  uint16_t bme_n;
  double tvoc;
  double eco2;   uint16_t ccs_n;
  double hcho;   uint16_t sfa_n;
};
static SensorAccum accum;  // statique → initialisé à zéro au boot

static void accumReset() { accum = SensorAccum{}; }

// Moyenne entière arrondie d'une somme positive (CO2/COV/eCO2 sont tous >= 0).
static int avgRoundI(double sum, uint16_t n) { return (int)(sum / n + 0.5); }

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

// ── CO2 : MH-Z19 ↔ SenseAir S8/S88 ─────────────────────────────────────────
// Les deux capteurs partagent le MÊME UART (mhzSerial, 9600 8N1) : un seul
// connecteur CO2 sur la carte. Les en-têtes diffèrent (MH-Z19 commence ses
// trames par 0xFF, la SenseAir répond en Modbus avec l'adresse 0xFE) et on
// draine le RX avant chaque commande : aucune diaphonie.
//
// Deux modes, pilotés par le réglage NVS Co2SensorChoice (settings.h) :
//   - AUTO           : on sonde les deux protocoles et on mémorise le gagnant,
//                      avec re-détection après CO2_REDETECT_AFTER échecs (hot-swap).
//   - MHZ19 / S88    : le protocole est imposé, on ne sonde JAMAIS l'autre.
//                      Utile quand l'auto-détection patine (capteur lent au
//                      démarrage) et pour un parc dont on connaît le matériel.
// `co2Sensor` ci-dessous reste le RÉSULTAT courant (ce qui a répondu), distinct
// du CHOIX utilisateur.
enum Co2Sensor { CO2_UNKNOWN, CO2_MHZ19, CO2_S88 };
static Co2Sensor co2Sensor = CO2_UNKNOWN;
static uint8_t co2FailStreak = 0;
static const uint8_t CO2_REDETECT_AFTER = 3;  // échecs consécutifs avant re-détection (hot-swap)
static Co2SensorChoice prevCo2Choice = CO2_CHOICE_AUTO;  // pour détecter un changement à chaud

static const char* co2SensorName(Co2Sensor s) {
  switch (s) {
    case CO2_MHZ19: return "MH-Z19";
    case CO2_S88:   return "SenseAir S8/S88";
    default:        return "?";
  }
}

// CRC-16 Modbus RTU (poly 0xA001), octets de poids faible en premier.
static uint16_t modbusCrc16(const uint8_t* buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
  }
  return crc;
}

// Tente une lecture MH-Z19 (lib WifWaf). Renvoie true + *out si valide.
static bool mhz19TryRead(int* out) {
  int co2 = mhz19.getCO2();
  if (mhz19.errorCode != RESULT_OK) return false;
  if (co2 < MHZ_CO2_MIN || co2 > MHZ_CO2_MAX) return false;
  *out = co2;
  return true;
}

// Tente une lecture SenseAir S8/S88 (Modbus RTU brut sur mhzSerial).
// Commande : lecture du registre d'entrée 0x0003 (CO2 ppm), 1 registre.
// Réponse attendue : 7 octets [0xFE 0x04 0x02 hi lo crcLo crcHi].
static bool s88TryRead(int* out) {
  static const uint8_t cmd[8] = {0xFE, 0x04, 0x00, 0x03, 0x00, 0x01, 0xD5, 0xC5};

  while (mhzSerial.available()) mhzSerial.read();   // draine le RX (vieilles trames)
  mhzSerial.write(cmd, sizeof(cmd));
  mhzSerial.flush();

  uint8_t resp[7];
  int n = 0;
  unsigned long t0 = millis();
  while (n < (int)sizeof(resp) && millis() - t0 < 250) {
    if (mhzSerial.available()) resp[n++] = mhzSerial.read();
  }
  if (n < (int)sizeof(resp)) return false;                 // pas de réponse complète
  if (resp[0] != 0xFE || resp[1] != 0x04 || resp[2] != 0x02) return false;  // en-tête

  uint16_t crc = modbusCrc16(resp, 5);                     // CRC sur addr+func+len+data
  if ((uint8_t)(crc & 0xFF) != resp[5] || (uint8_t)(crc >> 8) != resp[6]) return false;

  int co2 = (resp[3] << 8) | resp[4];
  if (co2 < MHZ_CO2_MIN || co2 > MHZ_CO2_MAX) return false;
  *out = co2;
  return true;
}

// Lit le CO2 du capteur détecté (ou sonde les deux si type inconnu) et écrit
// data.co2 / data.co2_ok. Mémorise le type pour ne pas re-sonder à chaque fois ;
// après CO2_REDETECT_AFTER échecs d'affilée, repasse en re-détection (hot-swap).
static void readCO2() {
  Logger.println("[CO2] Reading...");

  const Co2SensorChoice choice = settingsGetSensors().co2_sensor;

  // Choix manuel : on impose le protocole. On écrase `co2Sensor` à chaque cycle
  // plutôt qu'une seule fois, pour que le mode reste tenu même après un échec
  // (la branche d'erreur plus bas ne remet en CO2_UNKNOWN qu'en mode AUTO).
  if (choice == CO2_CHOICE_MHZ19)     co2Sensor = CO2_MHZ19;
  else if (choice == CO2_CHOICE_S88)  co2Sensor = CO2_S88;

  int co2 = 0;
  bool ok = false;

  if (co2Sensor == CO2_MHZ19) {
    ok = mhz19TryRead(&co2);
  } else if (co2Sensor == CO2_S88) {
    ok = s88TryRead(&co2);
  } else {
    // AUTO, type encore inconnu : on sonde le MH-Z19 d'abord (le plus courant),
    // puis la SenseAir.
    if (mhz19TryRead(&co2))      { co2Sensor = CO2_MHZ19; ok = true; }
    else if (s88TryRead(&co2))   { co2Sensor = CO2_S88;   ok = true; }
    if (ok) Logger.printf("  Capteur CO2 detecte: %s\n", co2SensorName(co2Sensor));
  }

  if (ok) {
    data.co2 = co2;
    data.co2_ok = true;
    co2FailStreak = 0;
    Logger.printf("  CO2 (%s): %d ppm\n", co2SensorName(co2Sensor), co2);
  } else {
    data.co2_ok = false;
    // En mode forcé, la re-détection n'a aucun sens : l'utilisateur a déclaré
    // quel capteur est monté, un silence est une panne/un warm-up, pas un
    // hot-swap. On garde donc le protocole et on log simplement l'échec.
    if (choice == CO2_CHOICE_AUTO &&
        co2Sensor != CO2_UNKNOWN && ++co2FailStreak >= CO2_REDETECT_AFTER) {
      Logger.printf("  %s muet x%u — re-detection au prochain cycle\n",
                    co2SensorName(co2Sensor), co2FailStreak);
      co2Sensor = CO2_UNKNOWN;
      co2FailStreak = 0;
    } else {
      Logger.printf("  Pas de mesure CO2 (%s absent ou en warm-up)\n",
                    co2Sensor == CO2_UNKNOWN ? "capteur" : co2SensorName(co2Sensor));
    }
  }
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

  // 1) Ping I2C (CCS811 = adresse 0x5A par defaut, 0x5B en option). C'est le
  // "il est là ?" : si ça ACK, le capteur est PRÉSENT et l'UI ne dira jamais
  // "non détecté" — au pire "préchauffage".
  bool present = i2cProbe(0x5A) || i2cProbe(0x5B);

  if (!present) {
    if (ccsFound) Logger.println("  Capteur perdu (etait present, debranche ?)");
    else          Logger.println("  Capteur absent (aucun ACK I2C 0x5A/0x5B)");
    ccsFound = false;
    data.ccs_ok = false;
    data.ccs_state = SENSOR_ABSENT;
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
      // Présent en I2C mais l'appli interne n'est pas prête (APP_VALID) :
      // transitoire au boot, on traite en chauffe plutôt qu'en absence.
      Logger.println("  Init echoue malgre I2C present (APP pas prete ?)");
      data.ccs_ok = false;
      data.ccs_state = SENSOR_WARMING;
      Logger.println();
      return;
    }
  }

  // 3) Présent mais pas encore de mesure (DATA_READY non levé) → on attend
  // le 1er échantillon. Présent en I2C, donc surtout PAS "non détecté".
  if (!ccs.available()) {
    data.ccs_ok = false;
    data.ccs_state = SENSOR_WARMING;
    Logger.println("  Present, donnees pas encore pretes (en attente)");
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
    data.ccs_state = SENSOR_OK;   // détecté + mesure fraîche = OK direct
    Logger.printf("  TVOC:  %d ppb\n", data.tvoc);
    Logger.printf("  eCO2:  %d ppm\n", data.eco2);
  } else {
    // Présent mais lecture KO transitoire : on reste en chauffe, pas en absence.
    data.ccs_ok = false;
    data.ccs_state = SENSOR_WARMING;
    Logger.println("  Erreur de lecture (transitoire)");
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

  // UART sensors: always open the port at boot regardless of the toggle's
  // default state. The toggle gates the *read*, not the hardware lifecycle.
  // This is what lets a user re-enable a sensor that started disabled (via
  // its SENSOR_*_DEFAULT or a previous UI toggle-off) and get it working on
  // the next cycle without a reboot — the lazy re-arm in sensorsSample()/
  // sensorsFinalize() just drains the RX buffer and re-binds the library. We never call
  // HardwareSerial::end() (long bug history on ESP32).
  nextpmSerial.begin(NEXTPM_BAUD, SERIAL_8E1, NEXTPM_RX, NEXTPM_TX);
  nextpm.begin(NEXTPM_ADDR, nextpmSerial);
  Logger.println(cfg.npm_enabled ? "NextPM init OK (Modbus RTU)"
                                 : "NextPM init OK (Modbus RTU, disabled — toggle in UI to enable)");
  prevNpmEnabled = cfg.npm_enabled;

  // UART CO2 partagé MH-Z19 / SenseAir S8-S88. autoCalibration(false) ne
  // concerne QUE le MH-Z19 (commande propriétaire 0x79) : la SenseAir garde son
  // ABC d'usine (180 h), réglable seulement en Modbus sur HR32 @0x001F.
  mhzSerial.begin(MHZ19_BAUD, SERIAL_8N1, MHZ19_RX, MHZ19_TX);
  mhz19.begin(mhzSerial);
  mhz19.autoCalibration(false);
  Logger.printf(cfg.mhz19_enabled ? "CO2 init OK — %s\n"
                                  : "CO2 init OK — %s (disabled — toggle in UI to enable)\n",
                settingsCo2SensorLabel(cfg.co2_sensor));
  prevMhzEnabled = cfg.mhz19_enabled;
  prevCo2Choice  = cfg.co2_sensor;

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
  co2Sensor = CO2_UNKNOWN;   // repart d'une détection vierge (readCO2 réimpose le choix si forcé)
  co2FailStreak = 0;
  Logger.println("[CO2] UART re-arme");
}

// Lit les capteurs "spot" (CO2, T/H/P, COV/eCO2, HCHO) et accumule chaque
// lecture valide dans `accum`. Appelé toutes les SENSOR_SAMPLE_INTERVAL.
// Les PM (NextPM) ne sont PAS lus ici : leur moyenne 1 min vient du capteur et
// est lue une fois par envoi dans sensorsFinalize().
//
// L'ordre BME280 -> CCS811 est conservé : readCCS811() utilise la temp/hum du
// BME280 (data.bme_ok) pour la compensation environnementale.
void sensorsSample() {
  const SensorSettings& cfg = settingsGetSensors();

  if (cfg.mhz19_enabled) {
    if (!prevMhzEnabled) {
      rearmMhzUart();
    } else if (cfg.co2_sensor != prevCo2Choice) {
      // L'utilisateur vient de changer de capteur dans l'UI : on repart d'un RX
      // propre et d'une détection vierge, sinon le protocole précédent resterait
      // collé (ou pire, un reliquat de trame serait relu comme une réponse).
      rearmMhzUart();
      Logger.printf("[CO2] Choix capteur -> %s\n", settingsCo2SensorLabel(cfg.co2_sensor));
    }
    readCO2();
    if (data.co2_ok) { accum.co2 += data.co2; accum.co2_n++; }
  }
  prevMhzEnabled = cfg.mhz19_enabled;
  prevCo2Choice  = cfg.co2_sensor;

  if (cfg.bme280_enabled) {
    readBME280();
    if (data.bme_ok) {
      accum.temp  += data.temperature;
      accum.hum   += data.humidity;
      accum.press += data.pressure;
      accum.bme_n++;
    }
  }

  if (cfg.ccs811_enabled) {
    readCCS811();
    if (data.ccs_ok) {
      accum.tvoc += data.tvoc;
      accum.eco2 += data.eco2;
      accum.ccs_n++;
    }
  }

  if (cfg.sfa40_enabled) {
    readSFA40();
    if (data.sfa40_ok) { accum.hcho += data.hcho; accum.sfa_n++; }
  }
}

// Lit les PM (registre 1 min du NextPM), calcule la moyenne des échantillons
// "spot" accumulés depuis le dernier envoi, publie le snapshot dans `published`
// puis remet les accumulateurs à zéro. Appelé à chaque DATA_SEND_INTERVAL.
//
// Politique d'erreur (plus robuste que ModuleAir-Next-Gen, qui exigeait un
// nombre EXACT d'échantillons) : on publie la moyenne dès qu'AU MOINS UN
// échantillon valide a été collecté ; si zéro (capteur absent, désactivé, ou
// en warm-up toute la fenêtre), le flag _ok passe à false.
void sensorsFinalize() {
  const SensorSettings& cfg = settingsGetSensors();

  // ── PM : lecture unique du NextPM (registre _1MIN = moyenne capteur) ──
  if (cfg.npm_enabled) {
    if (!prevNpmEnabled) rearmNpmUart();
    readNextPM();
  } else {
    data.pm_ok = false;
  }
  prevNpmEnabled = cfg.npm_enabled;

  // Recopie l'état PM tel quel (aucune moyenne firmware sur les PM).
  published.pm1       = data.pm1;
  published.pm25      = data.pm25;
  published.pm10      = data.pm10;
  published.npmStatus = data.npmStatus;
  published.pm_ok     = data.pm_ok;

  // ── CO2 (MH-Z19) ──
  if (accum.co2_n > 0) {
    published.co2 = avgRoundI(accum.co2, accum.co2_n);
    published.co2_ok = true;
  } else {
    published.co2_ok = false;
  }

  // ── BME280 (température / humidité / pression) ──
  if (accum.bme_n > 0) {
    published.temperature = accum.temp  / accum.bme_n;
    published.humidity    = accum.hum   / accum.bme_n;
    published.pressure    = accum.press / accum.bme_n;
    published.bme_ok = true;
  } else {
    published.bme_ok = false;
  }

  // ── COV / eCO2 (CCS811) ──
  if (accum.ccs_n > 0) {
    published.tvoc = avgRoundI(accum.tvoc, accum.ccs_n);
    published.eco2 = avgRoundI(accum.eco2, accum.ccs_n);
    published.ccs_ok = true;
  } else {
    published.ccs_ok = false;
  }
  // L'état présence/chauffe est INSTANTANÉ (dernière lecture), pas moyenné :
  // l'UI veut savoir si le capteur répond MAINTENANT en I2C, pour ne pas crier
  // "non détecté" pendant la chauffe ni après un débranchement en fin de fenêtre.
  published.ccs_state = data.ccs_state;

  // ── Formaldéhyde (SFA40) ──
  if (accum.sfa_n > 0) {
    published.hcho = accum.hcho / accum.sfa_n;
    published.sfa40_ok = true;
  } else {
    published.sfa40_ok = false;
  }

  published.lastReadTime = millis();

  Logger.printf("[Sensors] Snapshot publie (echantillons/fenetre): "
                "CO2=%u BME=%u CCS=%u SFA=%u\n",
                (unsigned)accum.co2_n, (unsigned)accum.bme_n,
                (unsigned)accum.ccs_n, (unsigned)accum.sfa_n);

  accumReset();
}

// Quel capteur CO2 répond RÉELLEMENT en ce moment (résultat de détection, pas
// le réglage). Renvoie NULL tant que rien n'a répondu — l'UI web s'en sert pour
// afficher ce que le mode "Auto" a effectivement trouvé.
const char* sensorsGetCo2SensorName() {
  return co2Sensor == CO2_UNKNOWN ? NULL : co2SensorName(co2Sensor);
}

bool sensorsCo2DetectedIsS88() { return co2Sensor == CO2_S88; }

const SensorData& sensorsGetData() {
  return published;
}
