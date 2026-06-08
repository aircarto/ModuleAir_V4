#include "logo_storage.h"
#include "logos.h"
#include "logger.h"
#include <SPIFFS.h>

// ── Slot metadata ──────────────────────────────────────────────────────────

static const char* slotPath(LogoSlot s) {
  switch (s) {
    case LOGO_SLOT_MA: return "/logo_ma.bin";
    case LOGO_SLOT_AC: return "/logo_ac.bin";
    case LOGO_SLOT_AS: return "/logo_as.bin";
#ifdef BUILD_LAIRETMOI
    case LOGO_SLOT_LAM: return "/logo_lam.bin";
#endif
    default:           return nullptr;
  }
}

static const uint16_t* compiledDefault(LogoSlot s) {
  switch (s) {
    case LOGO_SLOT_MA: return logo_moduleair;
    case LOGO_SLOT_AC: return logo_aircarto;
    case LOGO_SLOT_AS: return logo_atmo;
#ifdef BUILD_LAIRETMOI
    case LOGO_SLOT_LAM: return logo_lairetmoi;
#endif
    default:           return nullptr;
  }
}

// ── Runtime buffers (single source of truth for display) ───────────────────

static uint16_t buf_ma[LOGO_PIXELS];
static uint16_t buf_ac[LOGO_PIXELS];
static uint16_t buf_as[LOGO_PIXELS];
#ifdef BUILD_LAIRETMOI
static uint16_t buf_lam[LOGO_PIXELS];
#endif

static uint16_t* slotBuffer(LogoSlot s) {
  switch (s) {
    case LOGO_SLOT_MA: return buf_ma;
    case LOGO_SLOT_AC: return buf_ac;
    case LOGO_SLOT_AS: return buf_as;
#ifdef BUILD_LAIRETMOI
    case LOGO_SLOT_LAM: return buf_lam;
#endif
    default:           return nullptr;
  }
}

// ── I/O helpers ────────────────────────────────────────────────────────────

// Write [3-byte magic + version + data] to `path` atomically:
// write to <path>.tmp first, then rename. Avoids leaving a partial file
// behind on power loss mid-upload.
static bool writeFileAtomic(const char* path, const uint8_t* data, size_t len) {
  String tmp = String(path) + ".tmp";

  File f = SPIFFS.open(tmp, "w");
  if (!f) {
    Logger.printf("[LogoStorage] open(%s) failed\n", tmp.c_str());
    return false;
  }

  const uint8_t header[4] = { 'M', 'A', 'L', LOGO_FMT_VERSION };
  bool ok = (f.write(header, 4) == 4) && (f.write(data, len) == len);
  f.close();

  if (!ok) {
    SPIFFS.remove(tmp);
    return false;
  }

  // SPIFFS.rename fails if the target exists, so remove the old file first.
  SPIFFS.remove(path);
  if (!SPIFFS.rename(tmp, path)) {
    SPIFFS.remove(tmp);
    return false;
  }
  return true;
}

// Load a logo file from SPIFFS into `dest`. Validates magic, version, size.
static bool readLogoFile(const char* path, uint16_t* dest) {
  File f = SPIFFS.open(path, "r");
  if (!f) return false;

  if (f.size() != LOGO_FILE_BYTES) {
    Logger.printf("[LogoStorage] %s wrong size (%u, expected %u)\n",
                  path, (unsigned)f.size(), (unsigned)LOGO_FILE_BYTES);
    f.close();
    return false;
  }

  uint8_t header[4];
  if (f.read(header, 4) != 4) { f.close(); return false; }
  if (header[0] != 'M' || header[1] != 'A' || header[2] != 'L') {
    Logger.printf("[LogoStorage] %s bad magic\n", path);
    f.close();
    return false;
  }
  if (header[3] != LOGO_FMT_VERSION) {
    Logger.printf("[LogoStorage] %s unsupported version %u\n", path, header[3]);
    f.close();
    return false;
  }

  size_t got = f.read((uint8_t*)dest, LOGO_DATA_BYTES);
  f.close();
  return got == LOGO_DATA_BYTES;
}

// ── Public API ─────────────────────────────────────────────────────────────

void logoStorageInit() {
  Logger.println("[LogoStorage] Init");

  if (!SPIFFS.begin(true /* formatOnFail */)) {
    Logger.println("[LogoStorage] SPIFFS mount failed; using compiled logos in RAM");
    for (int i = 0; i < LOGO_SLOT_COUNT; i++) {
      LogoSlot s = (LogoSlot)i;
      memcpy(slotBuffer(s), compiledDefault(s), LOGO_DATA_BYTES);
    }
    return;
  }

  for (int i = 0; i < LOGO_SLOT_COUNT; i++) {
    LogoSlot s = (LogoSlot)i;
    const char* path = slotPath(s);
    uint16_t*   buf  = slotBuffer(s);

    // Seed missing file from compiled default (first boot, or after factory wipe).
    if (!SPIFFS.exists(path)) {
      Logger.printf("[LogoStorage] Seeding %s from compiled default\n", path);
      if (!writeFileAtomic(path, (const uint8_t*)compiledDefault(s), LOGO_DATA_BYTES)) {
        Logger.printf("[LogoStorage] Seed failed for %s\n", path);
      }
    }

    // Load file into RAM buffer; fall back to compiled if anything is off.
    if (readLogoFile(path, buf)) {
      Logger.printf("[LogoStorage] Loaded %s\n", path);
    } else {
      Logger.printf("[LogoStorage] Falling back to compiled default for %s\n", path);
      memcpy(buf, compiledDefault(s), LOGO_DATA_BYTES);
    }
  }
}

const uint16_t* logoBufferGet(LogoSlot slot) {
  if (slot < 0 || slot >= LOGO_SLOT_COUNT) return nullptr;
  return slotBuffer(slot);
}

bool logoSave(LogoSlot slot, const uint8_t* data, size_t len) {
  if (slot < 0 || slot >= LOGO_SLOT_COUNT) return false;
  if (len != LOGO_DATA_BYTES) {
    Logger.printf("[LogoStorage] save rejected: got %u bytes, expected %u\n",
                  (unsigned)len, (unsigned)LOGO_DATA_BYTES);
    return false;
  }
  const char* path = slotPath(slot);
  if (!writeFileAtomic(path, data, len)) {
    Logger.printf("[LogoStorage] save failed for %s\n", path);
    return false;
  }
  memcpy(slotBuffer(slot), data, LOGO_DATA_BYTES);
  Logger.printf("[LogoStorage] Saved %s (%u bytes)\n", path, (unsigned)len);
  return true;
}

bool logoReset(LogoSlot slot) {
  if (slot < 0 || slot >= LOGO_SLOT_COUNT) return false;
  const char* path = slotPath(slot);

  if (!writeFileAtomic(path, (const uint8_t*)compiledDefault(slot), LOGO_DATA_BYTES)) {
    Logger.printf("[LogoStorage] reset failed for %s\n", path);
    return false;
  }
  memcpy(slotBuffer(slot), compiledDefault(slot), LOGO_DATA_BYTES);
  Logger.printf("[LogoStorage] Reset %s to compiled default\n", path);
  return true;
}
