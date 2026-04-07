#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

void loggerInit();

// Itère sur les lignes stockées (de la plus ancienne à la plus récente).
// Le callback reçoit chaque ligne (sans '\n' final).
// Permet de streamer les logs sans construire une grosse String en heap.
void loggerForEachLine(void (*cb)(const char* line));

// Classe Print custom qui écrit sur Serial + buffer circulaire
class LoggerPrint : public Print {
public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;
};

extern LoggerPrint Logger;

#endif
