#include "logger.h"

// Buffer circulaire en RAM (DRAM statique).
// Cout : LOG_MAX_LINES * LOG_MAX_LINE_LEN octets.
// 200 * 120 = 24000 octets (~24 KB), confortable sur ESP32.
#define LOG_MAX_LINES 200
#define LOG_MAX_LINE_LEN 120

static char logBuffer[LOG_MAX_LINES][LOG_MAX_LINE_LEN];
static uint32_t logSeqs[LOG_MAX_LINES];  // numero de sequence par ligne
static int logHead = 0;       // prochaine ligne à écrire
static int logCount = 0;      // nombre de lignes stockées
static int logLinePos = 0;    // position dans la ligne courante
static uint32_t logSeq = 0;   // compteur monotone (incremente a chaque ligne)

LoggerPrint Logger;

void loggerInit() {
  memset(logBuffer, 0, sizeof(logBuffer));
  memset(logSeqs, 0, sizeof(logSeqs));
  logHead = 0;
  logCount = 0;
  logLinePos = 0;
  logSeq = 0;
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

uint32_t loggerCurrentSeq() {
  return logSeq;
}

void loggerForEachLineSince(uint32_t sinceSeq, void (*cb)(uint32_t seq, const char* line)) {
  if (!cb) return;
  int start = (logCount < LOG_MAX_LINES) ? 0 : logHead;
  int total = (logCount < LOG_MAX_LINES) ? logCount : LOG_MAX_LINES;
  for (int i = 0; i < total; i++) {
    int idx = (start + i) % LOG_MAX_LINES;
    if (logSeqs[idx] > sinceSeq) cb(logSeqs[idx], logBuffer[idx]);
  }
}

size_t LoggerPrint::write(uint8_t c) {
  // Toujours écrire sur Serial
  Serial.write(c);

  // Stocker dans le buffer circulaire
  if (c == '\n') {
    // Terminer la ligne courante
    logBuffer[logHead][logLinePos] = '\0';
    logSeqs[logHead] = ++logSeq;   // seq monotone de cette ligne
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
