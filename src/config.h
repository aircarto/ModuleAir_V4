#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define FIRMWARE_VERSION "0.0.1"

// WiFi AP
#define AP_PASSWORD "moduleaircfg"
#define MDNS_NAME   "moduleair"
#define WIFI_CONNECT_TIMEOUT 15000

// Data server
#define DATA_SERVER_URL "https://data.moduleair.fr/wifi_newDriver2026.php?device_type=ModuleAir"
#define DATA_SEND_INTERVAL 60000  // 60 secondes

// OTA Update
#define OTA_UPDATE_URL "https://gestion.aircarto.fr/api/ota/ModuleAir"
// Le serveur héberge :
//   {OTA_UPDATE_URL}/version.txt   → contient juste "0.7.0" (la dernière version)
//   {OTA_UPDATE_URL}/firmware.bin  → le binaire compilé

// NextPM (UART1, Modbus RTU)
#define NEXTPM_RX 39
#define NEXTPM_TX 32
#define NEXTPM_BAUD 115200
#define NEXTPM_ADDR 0x01

// MH-Z19 (UART2)
#define MHZ19_RX 36
#define MHZ19_TX 27
#define MHZ19_BAUD 9600

// BME280 (I2C)
#define I2C_SDA 21
#define I2C_SCL 22

// LEDs WS2812B
#define LED_PIN 25
#define LED_COUNT 8

// Device ID (computed from MAC at startup)
extern String deviceId;     // ex: "AABBCCDDEEFF"
extern String apSSID;       // ex: "ModuleAir-DDEEFF"

void configInit();

#endif
