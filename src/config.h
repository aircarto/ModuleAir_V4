#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define FIRMWARE_VERSION "0.4.0"

// ── Default sensor enable state ─────────────────────────────────────────────
// These are the FACTORY DEFAULTS for each sensor's enabled flag — the value
// used the very first time the device boots (when NVS has no stored toggle
// yet). They are NOT permanent locks:
//   - true  -> sensor starts enabled
//   - false -> sensor starts disabled, exactly as if the user had toggled it
//              off in the web UI. The hardware is still initialised at boot,
//              so the user can go into "Capteurs actifs" and toggle it back
//              on at runtime and it starts working on the next measurement
//              cycle (no reboot needed).
// Once the user touches a toggle in the UI, that choice is persisted to NVS
// and wins over these defaults on subsequent boots.
#define SENSOR_NPM_DEFAULT     true
#define SENSOR_MHZ19_DEFAULT   true
#define SENSOR_BME280_DEFAULT  false
#define SENSOR_CCS811_DEFAULT  false
#define SENSOR_SFA40_DEFAULT   false

// ── Default matrix screen / logo state ──────────────────────────────────────
// First-boot defaults for the matrix rotation — same semantics as the sensor
// defaults above: they set the initial enabled state, stay toggleable at
// runtime via the "Ecrans matrice" web UI, and NVS wins once the user touches
// a toggle. Edit these to choose what a freshly-flashed board shows. (On a
// board that's already been booted with a stored toggle, NVS wins — use the
// web UI toggle, or do a WiFi/NVS reset to re-apply these.)
//
// AtmoSud logo is OFF by default — flip LOGO_ATMOSUD_DEFAULT to true if you
// want it back in the rotation.
#define SCREEN_PM1_DEFAULT      true
#define SCREEN_PM25_DEFAULT     true
#define SCREEN_PM10_DEFAULT     true
#define SCREEN_CO2_DEFAULT      true
#define SCREEN_TEMP_DEFAULT     true
#define SCREEN_HUMI_DEFAULT     true
#define SCREEN_TVOC_DEFAULT     true
#define SCREEN_HCHO_DEFAULT     true
#define LOGO_MODULEAIR_DEFAULT  true
#define LOGO_AIRCARTO_DEFAULT   true
// Le logo AtmoSud suit la variante de build (cf. platformio.ini) :
//   build atmosud / lairetmoi -> ON par défaut  |  build classique -> OFF
#if defined(BUILD_ATMOSUD) || defined(BUILD_LAIRETMOI)
  #define LOGO_ATMOSUD_DEFAULT  true
#else
  #define LOGO_ATMOSUD_DEFAULT  false
#endif
// Le logo « L'Air et Moi » n'existe que sur la build lairetmoi
// (-DBUILD_LAIRETMOI). ON par défaut sur cette build ; absent ailleurs.
#ifdef BUILD_LAIRETMOI
  #define LOGO_LAIRETMOI_DEFAULT  true
#endif

// WiFi AP
// The hotspot is intentionally open (no password). The first-time-config
// flow puts the user on a captive portal that ONLY exposes a network
// configuration UI — no sensitive data, no persistent local state to
// protect — so a passphrase would just add friction without security benefit.
#define MDNS_NAME   "moduleair"
#define WIFI_CONNECT_TIMEOUT 15000

// ── Default WiFi credentials (first-boot auto-connect) ──────────────────────
// On a factory-fresh device (NVS empty, no SSID ever saved), the firmware
// tries these credentials before falling back to the captive-portal config
// mode. Handy for lab/QA: a freshly-flashed board auto-joins the bench
// network on first power-up. These are TRANSIENT — never written to NVS, so
// the moment the user configures a real network through the captive portal,
// that NVS value wins and the default is forgotten on every later boot. If
// the default network isn't in range, the connect attempt fails and the
// device drops into the normal AP-config flow. Set DEFAULT_WIFI_SSID to ""
// to disable the default entirely and go straight to config mode.
#define DEFAULT_WIFI_SSID      "AirLab"
#define DEFAULT_WIFI_PASSWORD  "123plou"

// Data server (AirCarto — toujours actif)
#define DATA_SERVER_URL "https://data.moduleair.fr/wifi_newDriver2026.php?device_type=ModuleAir"
#define DATA_SEND_INTERVAL 60000  // 60 secondes

// Sous-échantillonnage capteurs (à la manière de ModuleAir-Next-Gen).
// On lit les capteurs "spot" (CO2, BME280, CCS811, SFA40) toutes les
// SENSOR_SAMPLE_INTERVAL et on envoie la MOYENNE des échantillons valides de
// la fenêtre à chaque DATA_SEND_INTERVAL (≈6 échantillons/minute). 10 s est
// aussi la cadence interne du CCS811 (DRIVE_MODE_10SEC), c'est l'intervalle
// naturel. Les PM (NextPM) ne sont PAS moyennés ici : le capteur expose déjà
// une moyenne glissante 1 min (registres _1MIN), lue une fois par envoi —
// la moyenner à nouveau ne ferait que la sur-lisser et la retarder.
#define SENSOR_SAMPLE_INTERVAL 10000  // 10 secondes

// ── Serveur AtmoSud (MicroSpot) ──────────────────────────────────────────────
// Reproduit la cible et le format de ModuleAir-Next-Gen : envoi HTTPS du payload
// "sensordatavalues".
//
// L'URL (token inclus) N'EST PLUS dans ce fichier : elle vit dans secrets.ini
// (gitignore) et est injectée au build par secrets_inject.py sous forme de
// -DATMOSUD_SERVER_URL. Sans secrets.ini, la branche AtmoSud est compile-out
// (zéro trace de l'URL/token dans le binaire). Même architecture que NebuleAir.
//
// L'ACTIVATION est PAR CAPTEUR, write-once, jamais togglable :
//   1. stamp NVS (namespace "atmosud", clé "stamped") écrit au 1er boot par le
//      binaire de provisioning (env atmosud_provision, -DFACTORY_ATMOSUD=1).
//      Survit aux OTA et au reset WiFi. SEULE voie pour un capteur neuf —
//      basculer un capteur déjà déployé = reflash USB avec cet env.
//   2. migration : l'ancien flag NVS "server/atmosud" == 1 (architecture
//      pré-2026-06) est converti en stamp au boot — les capteurs AtmoSud déjà
//      en service continuent d'envoyer après mise à jour. Cf. data_sender.cpp.
// -DBUILD_ATMOSUD ne pilote plus que le défaut d'affichage du logo (ci-dessus).
#define ATMOSUD_SOFTWARE_VERSION "ModuleAirV4-V" FIRMWARE_VERSION

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
