#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

void loggerInit();

// Itère sur les lignes stockées (de la plus ancienne à la plus récente).
// Le callback reçoit chaque ligne (sans '\n' final).
// Permet de streamer les logs sans construire une grosse String en heap.
void loggerForEachLine(void (*cb)(const char* line));

// Numero de sequence global de la derniere ligne ecrite (jamais reset, monotone).
// Sert de curseur pour le tail incremental de l'interface web.
uint32_t loggerCurrentSeq();

// Variante de loggerForEachLine qui ne renvoie que les lignes dont le seq est
// strictement superieur a sinceSeq. Pour tail incremental ("fetch since N").
void loggerForEachLineSince(uint32_t sinceSeq, void (*cb)(uint32_t seq, const char* line));

// Classe Print custom qui écrit sur Serial + buffer circulaire
class LoggerPrint : public Print {
public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t *buffer, size_t size) override;
};

extern LoggerPrint Logger;

#endif
