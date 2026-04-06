#include "logger.h"

#define LOG_MAX_LINES 50
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

String loggerGetAll() {
  String result;
  result.reserve(LOG_MAX_LINES * 80);

  int start = (logCount < LOG_MAX_LINES) ? 0 : logHead;
  int total = min(logCount, LOG_MAX_LINES);

  for (int i = 0; i < total; i++) {
    int idx = (start + i) % LOG_MAX_LINES;
    result += logBuffer[idx];
    result += '\n';
  }
  return result;
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
