#include "logger.h"

// Buffer circulaire en RAM (DRAM statique).
// Cout : LOG_MAX_LINES * LOG_MAX_LINE_LEN octets.
// 200 * 120 = 24000 octets (~24 KB), confortable sur ESP32.
#define LOG_MAX_LINES 200
#define LOG_MAX_LINE_LEN 120

static char logBuffer[LOG_MAX_LINES][LOG_MAX_LINE_LEN];
static int logHead = 0;       // prochaine ligne à écrire
static int logCount = 0;      // nombre de lignes stockées
static int logLinePos = 0;    // position dans la ligne courante

LoggerPrint Logger;

void loggerInit() {
  memset(logBuffer, 0, sizeof(logBuffer));
  logHead = 0;
  logCount = 0;
  logLinePos = 0;
}

void loggerForEachLine(void (*cb)(const char* line)) {
  if (!cb) return;
  int start = (logCount < LOG_MAX_LINES) ? 0 : logHead;
  int total = (logCount < LOG_MAX_LINES) ? logCount : LOG_MAX_LINES;
  for (int i = 0; i < total; i++) {
    int idx = (start + i) % LOG_MAX_LINES;
    cb(logBuffer[idx]);
  }
}

size_t LoggerPrint::write(uint8_t c) {
  // Toujours écrire sur Serial
  Serial.write(c);

  // Stocker dans le buffer circulaire
  if (c == '\n') {
    // Terminer la ligne courante
    logBuffer[logHead][logLinePos] = '\0';
    logHead = (logHead + 1) % LOG_MAX_LINES;
    logCount++;
    logLinePos = 0;
  } else if (c != '\r' && logLinePos < LOG_MAX_LINE_LEN - 1) {
    logBuffer[logHead][logLinePos++] = (char)c;
  }

  return 1;
}

size_t LoggerPrint::write(const uint8_t *buffer, size_t size) {
  for (size_t i = 0; i < size; i++) {
    write(buffer[i]);
  }
  return size;
}
