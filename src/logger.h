#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

void loggerInit();
String loggerGetAll();

// Classe Print custom qui écrit sur Serial + buffer circulaire
class LoggerPrint : public Print {
public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;
};

extern LoggerPrint Logger;

#endif
