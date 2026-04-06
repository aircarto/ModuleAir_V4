#include <Arduino.h>
#include <Wire.h>
#include <ModbusMaster.h>
#include <MHZ19.h>
#include <Adafruit_BME280.h>
#include <Adafruit_CCS811.h>
#include "config.h"
#include "sensors.h"
#include "logger.h"

static HardwareSerial nextpmSerial(1);
static ModbusMaster nextpm;

static HardwareSerial mhzSerial(2);
static MHZ19 mhz19;

static Adafruit_BME280 bme;
static bool bmeFound = false;

static Adafruit_CCS811 ccs;
static bool ccsFound = false;

static SensorData data = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, false, false, false, false, 0};

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

void sensorsInit() {
  nextpmSerial.begin(NEXTPM_BAUD, SERIAL_8E1, NEXTPM_RX, NEXTPM_TX);
  nextpm.begin(NEXTPM_ADDR, nextpmSerial);
  Logger.println("NextPM init OK (Modbus RTU)");

  mhzSerial.begin(MHZ19_BAUD, SERIAL_8N1, MHZ19_RX, MHZ19_TX);
  mhz19.begin(mhzSerial);
  mhz19.autoCalibration(false);
  Logger.println("MH-Z19 init OK");

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
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

  if (ccs.begin()) {
    ccsFound = true;
    ccs.setDriveMode(CCS811_DRIVE_MODE_10SEC);
    Logger.println("CCS811 init OK (0x5A)");
  } else {
    Logger.println("CCS811 not found!");
  }
}

void sensorsRead() {
  readNextPM();
  readMHZ19();
  readBME280();
  readCCS811();
  data.lastReadTime = millis();
}

const SensorData& sensorsGetData() {
  return data;
}
