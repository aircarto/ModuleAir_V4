#include "logger.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern "C" {
  #include "esp_log.h"
}

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

// Mutex RECURSIF protegeant tout l'etat ci-dessus. Trois contextes ecrivent
// dans le logger en parallele :
//   1. la boucle Arduino principale (display, data_sender, wifi...)
//   2. la tache networkMonitorTask (core 1, logs [NetMon] toutes les 15s)
//   3. la pile WiFi/lwIP via le hook esp_log_set_vprintf (customVprintf)
// Sans verrou, deux ecritures simultanees se melangent dans le meme slot
// logBuffer[logHead] : lignes corrompues ("[Disps) Screen...") et perdues.
// Le symptome le plus visible etait la disparition du bloc [AirCarto] POST, qui
// s'imprime exactement quand la pile TLS/WiFi est la plus bavarde.
//
// Recursif car write(buffer,size) appelle write(uint8_t) en boucle, et les
// methodes haut niveau (println = print + "\r\n") s'imbriquent : le meme
// thread doit pouvoir reprendre le verrou qu'il detient deja.
//
// Granularite = la LIGNE, pas le caractere. On prend une reference
// supplementaire sur le 1er octet d'une ligne et on ne la rend qu'au '\n'
// final. Tant qu'une tache n'a pas termine sa ligne, les autres taches
// bloquent a l'entree de write() : impossible d'inserer "[NetMon]..." entre
// le texte d'une ligne et son '\n'.
static SemaphoreHandle_t logMutex = nullptr;
static bool lineOpen = false;  // une ligne est en cours (verrou ligne detenu)

LoggerPrint Logger;

// Custom vprintf hook for ESP-IDF logs. The framework's log_e/log_w/log_i
// macros and any code calling esp_log_write* normally go straight to UART
// via ets_printf, bypassing our LoggerPrint class — that's why the web
// /logs endpoint was missing entries like `[E][WiFiClient.cpp:429] ...`
// that the USB serial monitor showed. By installing this hook we capture
// every framework log line into the same Logger pipeline, so the serial
// output and the web buffer stay strictly identical.
static int customVprintf(const char* fmt, va_list args) {
  char buf[256];
  int len = vsnprintf(buf, sizeof(buf), fmt, args);
  if (len > 0) {
    // Logger.print writes to Serial AND appends to the circular buffer.
    // We use a copy of args to format twice (vsnprintf consumed the
    // original), but since we already produced the formatted string in buf
    // we can just print it directly without re-formatting.
    Logger.print(buf);
  }
  return len;
}

void loggerInit() {
  // Creer le mutex AVANT d'armer le hook esp_log : un log framework pourrait
  // sinon arriver alors que logMutex est encore null.
  if (!logMutex) logMutex = xSemaphoreCreateRecursiveMutex();

  memset(logBuffer, 0, sizeof(logBuffer));
  memset(logSeqs, 0, sizeof(logSeqs));
  logHead = 0;
  logCount = 0;
  logLinePos = 0;
  logSeq = 0;
  lineOpen = false;

  // Redirect ESP-IDF logs through our Logger so they appear in /logs.
  esp_log_set_vprintf(customVprintf);
}

// Lectures (cote serveur web). Verrouillees aussi : une ecriture concurrente
// depuis netmon/lwIP pendant l'iteration ferait avancer logHead/logCount en
// plein milieu de la boucle (lignes sautees ou dupliquees dans le tail).
// Le verrou est recursif et detenu par la tache loop() : si sendContent()
// declenche un log framework sur CETTE meme tache, il reprend le verrou sans
// blocage.
void loggerForEachLine(void (*cb)(const char* line)) {
  if (!cb) return;
  if (logMutex) xSemaphoreTakeRecursive(logMutex, portMAX_DELAY);
  int start = (logCount < LOG_MAX_LINES) ? 0 : logHead;
  int total = (logCount < LOG_MAX_LINES) ? logCount : LOG_MAX_LINES;
  for (int i = 0; i < total; i++) {
    int idx = (start + i) % LOG_MAX_LINES;
    cb(logBuffer[idx]);
  }
  if (logMutex) xSemaphoreGiveRecursive(logMutex);
}

uint32_t loggerCurrentSeq() {
  return logSeq;
}

void loggerForEachLineSince(uint32_t sinceSeq, void (*cb)(uint32_t seq, const char* line)) {
  if (!cb) return;
  if (logMutex) xSemaphoreTakeRecursive(logMutex, portMAX_DELAY);
  int start = (logCount < LOG_MAX_LINES) ? 0 : logHead;
  int total = (logCount < LOG_MAX_LINES) ? logCount : LOG_MAX_LINES;
  for (int i = 0; i < total; i++) {
    int idx = (start + i) % LOG_MAX_LINES;
    if (logSeqs[idx] > sinceSeq) cb(logSeqs[idx], logBuffer[idx]);
  }
  if (logMutex) xSemaphoreGiveRecursive(logMutex);
}

size_t LoggerPrint::write(uint8_t c) {
  // Avant loggerInit() (Serial.begin -> premiers logs), pas de mutex : on
  // ecrit directement, mono-thread a ce stade.
  if (logMutex) xSemaphoreTakeRecursive(logMutex, portMAX_DELAY);

  // Ouvrir le "verrou ligne" sur le 1er octet stocke d'une ligne fraiche :
  // une reference recursive supplementaire, rendue seulement au '\n'. Tant
  // qu'elle est detenue, les autres taches bloquent a l'entree de write().
  if (logMutex && !lineOpen && logLinePos == 0 && c != '\n' && c != '\r') {
    xSemaphoreTakeRecursive(logMutex, portMAX_DELAY);
    lineOpen = true;
  }

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
    // Fin de ligne : relacher le verrou ligne pris a l'ouverture.
    if (logMutex && lineOpen) {
      lineOpen = false;
      xSemaphoreGiveRecursive(logMutex);
    }
  } else if (c != '\r') {
    // Ligne plus longue que le slot : on REPLIE sur le slot suivant au lieu
    // de jeter la suite. Les payloads JSON [AirCarto]/[AtmoSud] (~450 chars)
    // etaient tronques a 119 chars sur /logs alors que le moniteur serie les
    // montrait en entier. Le web affiche la ligne en segments de 119 chars :
    // contenu integral, zero RAM en plus. Le verrou ligne reste detenu
    // jusqu'au vrai '\n' : les segments d'une meme ligne restent contigus.
    if (logLinePos >= LOG_MAX_LINE_LEN - 1) {
      logBuffer[logHead][logLinePos] = '\0';
      logSeqs[logHead] = ++logSeq;
      logHead = (logHead + 1) % LOG_MAX_LINES;
      logCount++;
      logLinePos = 0;
    }
    logBuffer[logHead][logLinePos++] = (char)c;
  }

  if (logMutex) xSemaphoreGiveRecursive(logMutex);
  return 1;
}

size_t LoggerPrint::write(const uint8_t *buffer, size_t size) {
  // Prendre le verrou pour tout le buffer : les write(uint8_t) imbriques le
  // reprennent en recursif, donc le chunk entier reste atomique.
  if (logMutex) xSemaphoreTakeRecursive(logMutex, portMAX_DELAY);
  for (size_t i = 0; i < size; i++) {
    write(buffer[i]);
  }
  if (logMutex) xSemaphoreGiveRecursive(logMutex);
  return size;
}
