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
void sensorsRead();
const SensorData& sensorsGetData();

#endif
