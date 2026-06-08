#ifndef LOGO_STORAGE_H
#define LOGO_STORAGE_H

#include <Arduino.h>

// 64x32 RGB565 = 2048 pixels * 2 bytes = 4096 bytes
#define LOGO_PIXELS      2048
#define LOGO_DATA_BYTES  (LOGO_PIXELS * sizeof(uint16_t))

// File layout on SPIFFS:
//   [magic 'M','A','L'][version: 1][data: 4096 bytes RGB565 little-endian]
// = 4100 bytes
#define LOGO_FILE_BYTES  (4 + LOGO_DATA_BYTES)
#define LOGO_FMT_VERSION 1

enum LogoSlot {
  LOGO_SLOT_MA = 0,  // ModuleAir
  LOGO_SLOT_AC = 1,  // AirCarto
  LOGO_SLOT_AS = 2,  // AtmoSud
#ifdef BUILD_LAIRETMOI
  LOGO_SLOT_LAM,     // L'Air et Moi (build lairetmoi uniquement)
#endif
  LOGO_SLOT_COUNT
};

// Mount SPIFFS, seed missing logos from compiled defaults, load all slots into RAM buffers.
// Call once in setup() before displayInit().
void logoStorageInit();

// Active logo buffer for a slot. Always returns a valid pointer (RAM if SPIFFS loaded,
// else compiled default copied to RAM at init).
const uint16_t* logoBufferGet(LogoSlot slot);

// Overwrite a slot with new RGB565 data (must be exactly LOGO_DATA_BYTES).
// Writes atomically to SPIFFS (.tmp + rename) and updates the RAM buffer on success.
bool logoSave(LogoSlot slot, const uint8_t* rgb565_data, size_t len);

// Revert a slot back to the compiled factory default (overwrites SPIFFS file).
bool logoReset(LogoSlot slot);

#endif
