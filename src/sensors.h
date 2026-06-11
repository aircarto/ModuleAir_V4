#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

struct SensorData {
  float pm1;
  float pm25;
  float pm10;
  int co2;
  float temperature;
  float humidity;
  float pressure;
  int tvoc;
  int eco2;
  float hcho;       // formaldehyde in ppb (SFA40)
  uint8_t npmStatus;
  bool pm_ok;
  bool co2_ok;
  bool bme_ok;
  bool ccs_ok;
  bool sfa40_ok;
  unsigned long lastReadTime;
};

void sensorsInit();

// Lit les capteurs "spot" (CO2, BME280, CCS811, SFA40) et accumule chaque
// lecture valide. À appeler toutes les SENSOR_SAMPLE_INTERVAL.
void sensorsSample();

// Lit les PM (moyenne 1 min du NextPM), calcule la moyenne des échantillons
// accumulés depuis le dernier envoi, publie le snapshot puis remet les
// accumulateurs à zéro. À appeler à chaque DATA_SEND_INTERVAL.
void sensorsFinalize();

const SensorData& sensorsGetData();

#endif
