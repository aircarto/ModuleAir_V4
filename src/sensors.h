#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

// État de présence d'un capteur (CCS811). On ne se contente plus d'un booléen
// "ok/pas ok" : un capteur peut être PRÉSENT mais pas encore avoir sorti sa 1ère
// mesure. Distinguer les deux évite le faux "non détecté" rouge affiché juste
// après l'activation, alors que le capteur répond bien en I2C.
//  - ABSENT  : le ping I2C (0x5A/0x5B) ne répond pas      → "non détecté" (rouge)
//  - WARMING : ACK I2C OK mais DATA_READY pas encore levé → "en attente" (orange, bref)
//  - OK      : présent + mesure fraîche                    → la valeur (vert)
enum SensorPresence : uint8_t {
  SENSOR_ABSENT  = 0,
  SENSOR_WARMING = 1,
  SENSOR_OK      = 2,
};

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
  SensorPresence ccs_state;  // tri-état présence/chauffe du CCS811 (cf. enum SensorPresence)
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

// Nom du capteur CO2 actuellement détecté ("MH-Z19" / "SenseAir S8/S88"), ou
// NULL si aucun n'a encore répondu. La détection est automatique — c'est la
// seule source de vérité sur le modèle branché, il n'y a aucun réglage.
const char* sensorsGetCo2SensorName();

// true si le capteur CO2 qui répond actuellement est une SenseAir S8/S88.
bool sensorsCo2DetectedIsS88();

#endif
