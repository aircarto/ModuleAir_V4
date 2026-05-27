#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define FIRMWARE_VERSION "0.3.1"

// ── Compile-time sensor master switches ────────────────────────────────────
// Each flag is a build-time master switch: set to 0 to completely remove
// a sensor from the system. When a sensor is COMPILED-OUT (= 0):
//   - Its hardware is not initialised at boot (UART not opened, I2C not
//     probed, library .begin() never called) — pins stay free, no startup
//     latency for that sensor.
//   - settingsGetSensors() forces its runtime-enabled flag to false even
//     if NVS holds true (a previous build flipped it on, or user toggled).
//   - The web-UI toggle row appears greyed-out with an "(off en code)"
//     hint so the user understands it's a build choice, not a fault.
//   - sensorsRead() skips it, data_sender.cpp drops its JSON fields,
//     display.cpp drops its rotation screens — all driven by the same
//     forced-false runtime flag, no per-consumer #ifdef needed.
// Set to 1 to compile the sensor in (default — fully working sensor that
// the user can then enable/disable at runtime from the web UI).
#define SENSOR_NPM_COMPILED    1
#define SENSOR_MHZ19_COMPILED  1
#define SENSOR_BME280_COMPILED 1
#define SENSOR_CCS811_COMPILED 1
#define SENSOR_SFA40_COMPILED  1

// WiFi AP
// The hotspot is intentionally open (no password). The first-time-config
// flow puts the user on a captive portal that ONLY exposes a network
// configuration UI — no sensitive data, no persistent local state to
// protect — so a passphrase would just add friction without security benefit.
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

// Matrix LED HUB75 (SPI HSPI + control)
#define MATRIX_WIDTH  64
#define MATRIX_HEIGHT 32
#define P_LAT 25
#define P_A   17
#define P_B   33
#define P_C   4
#define P_D   12
#define P_E   15
#define P_OE  16

// Device ID (computed from MAC at startup)
extern String deviceId;     // ex: "AABBCCDDEEFF"
extern String apSSID;       // ex: "ModuleAir-DDEEFF"

void configInit();

#endif
