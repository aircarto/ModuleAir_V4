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

  uint16_t status = readU16(REG_STATUS);
  if (status != 0xFFFF) {
    data.npmStatus = (uint8_t)(status & 0xFF);
    Logger.printf("  Status: 0x%X\n", status);
  }

  uint32_t pm1_raw  = readU32(REG_PM1_1MIN);
  uint32_t pm25_raw = readU32(REG_PM25_1MIN);
  uint32_t pm10_raw = readU32(REG_PM10_1MIN);

  if (pm1_raw != 0xFFFFFFFF && pm25_raw != 0xFFFFFFFF && pm10_raw != 0xFFFFFFFF) {
    data.pm1  = pm1_raw / 1000.0;
    data.pm25 = pm25_raw / 1000.0;
    data.pm10 = pm10_raw / 1000.0;
    data.pm_ok = true;
    Logger.printf("  PM1.0:  %.3f ug/m3\n", data.pm1);
    Logger.printf("  PM2.5:  %.3f ug/m3\n", data.pm25);
    Logger.printf("  PM10:   %.3f ug/m3\n", data.pm10);
  } else {
    data.pm_ok = false;
  }

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

  if (co2 > 0) {
    data.co2 = co2;
    data.co2_ok = true;
    Logger.printf("  CO2:  %d ppm\n", co2);
  } else {
    data.co2_ok = false;
    Logger.println("  CO2:  error or warming up");
  }

  Logger.printf("  Temp (MH-Z19): %d C\n", mhz19.getTemperature());
  Logger.println();
}

static void readBME280() {
  Logger.println("[BME280] Reading...");

  if (!bmeFound) {
    data.bme_ok = false;
    Logger.println("  Sensor not found");
    Logger.println();
    return;
  }

  float temperature = bme.readTemperature();
  float humidity = bme.readHumidity();
  float pressure = bme.readPressure() / 100.0;

  if (isnan(temperature) || isnan(pressure)) {
    data.bme_ok = false;
    Logger.println("  Error reading BME280");
    Logger.println();
    return;
  }

  data.temperature = temperature;
  data.humidity = humidity;
  data.pressure = pressure;
  data.bme_ok = true;

  Logger.printf("  Temp:     %.1f C\n", temperature);
  Logger.printf("  Humidity: %.1f %%\n", humidity);
  Logger.printf("  Pressure: %.1f hPa\n", pressure);
  Logger.println();
}

static void readCCS811() {
  Logger.println("[CCS811] Reading...");

  if (!ccsFound) {
    data.ccs_ok = false;
    Logger.println("  Sensor not found");
    Logger.println();
    return;
  }

  if (!ccs.available()) {
    data.ccs_ok = false;
    Logger.println("  Data not available yet");
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
    Logger.println("  Error reading CCS811");
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

  if (!sfa40Found) {
    data.sfa40_ok = false;
    Logger.println("  Sensor not found");
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

  if (cfg.npm_enabled) {
    nextpmSerial.begin(NEXTPM_BAUD, SERIAL_8E1, NEXTPM_RX, NEXTPM_TX);
    nextpm.begin(NEXTPM_ADDR, nextpmSerial);
    Logger.println("NextPM init OK (Modbus RTU)");
  } else {
    Logger.println("NextPM disabled");
  }

  if (cfg.mhz19_enabled) {
    mhzSerial.begin(MHZ19_BAUD, SERIAL_8N1, MHZ19_RX, MHZ19_TX);
    mhz19.begin(mhzSerial);
    mhz19.autoCalibration(false);
    Logger.println("MH-Z19 init OK");
  } else {
    Logger.println("MH-Z19 disabled");
  }

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  if (cfg.bme280_enabled) {
    if (bme.begin(0x76)) {
      bmeFound = true;
      bme.setSampling(Adafruit_BME280::MODE_FORCED,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::FILTER_OFF);
      Logger.println("BME280 init OK (0x76)");
    } else if (bme.begin(0x77)) {
      bmeFound = true;
      bme.setSampling(Adafruit_BME280::MODE_FORCED,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::FILTER_OFF);
      Logger.println("BME280 init OK (0x77)");
    } else {
      Logger.println("BME280 not found!");
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
      Logger.println("CCS811 not found!");
    }
  } else {
    Logger.println("CCS811 disabled");
  }

  if (cfg.sfa40_enabled) {
    // Check if SFA40 is present on I2C
    Wire.beginTransmission(SFA40_ADDR);
    if (Wire.endTransmission() == 0) {
      sfa40Found = true;
      sfa40SendCmd(0x50D2);  // Stop (reset to known state)
      delay(50);
      sfa40SendCmd(0x00AC);  // Start Measurement
      sfa40Started = true;
      Logger.println("SFA40 init OK (0x5D)");
    } else {
      Logger.println("SFA40 not found!");
    }
  } else {
    Logger.println("SFA40 disabled");
  }
}

void sensorsRead() {
  const SensorSettings& cfg = settingsGetSensors();
  if (cfg.npm_enabled)    readNextPM();
  if (cfg.mhz19_enabled)  readMHZ19();
  if (cfg.bme280_enabled) readBME280();
  if (cfg.ccs811_enabled) readCCS811();
  if (cfg.sfa40_enabled)  readSFA40();
  data.lastReadTime = millis();
}

const SensorData& sensorsGetData() {
  return data;
}
