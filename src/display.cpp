#include <Arduino.h>
#include <Preferences.h>
#include <PxMatrix.h>
#include "config.h"
#include "display.h"
#include "sensors.h"
#include "logos.h"
#include "logger.h"

// ── Hardware ──

static uint8_t display_draw_time = 30;
static bool debugSplashEnabled = false;
static hw_timer_t *timer = NULL;
static SemaphoreHandle_t displaySem;

static PxMATRIX display(MATRIX_WIDTH, MATRIX_HEIGHT, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);

static void IRAM_ATTR displayISR() {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(displaySem, &woken);
  if (woken) portYIELD_FROM_ISR();
}

static void displayTask(void *param) {
  for (;;) {
    if (xSemaphoreTake(displaySem, portMAX_DELAY)) {
      display.display(display_draw_time);
    }
  }
}

// ── Colors ──

static const uint16_t COLOR_WHITE  = 0xFFFF;
static const uint16_t COLOR_CYAN   = 0x07FF;
static const uint16_t COLOR_BLUE   = 0x1CDF;
static const uint16_t COLOR_GREEN  = 0x07E0;
static const uint16_t COLOR_YELLOW = 0xFFE0;
static const uint16_t COLOR_ORANGE = 0xFBE0;
static const uint16_t COLOR_RED    = 0xF800;
static const uint16_t COLOR_GRAY   = 0x6B4D;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return display.color565(r, g, b);
}

// ── Data cycling state ──

static SensorData sensorCache = {};
static bool hasData = false;
static unsigned long lastScreenChange = 0;
static int currentScreen = 0;
#define SCREEN_INTERVAL 5000

// Screen IDs
enum Screen { SCR_PM25, SCR_PM10, SCR_CO2, SCR_TEMP, SCR_HUMI, SCR_COV, SCR_LOGO, SCR_COUNT };

// ── Helpers ──

static void drawImage(int x, int y, int h, int w, const uint16_t image[]) {
  int counter = 0;
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++)
      display.drawPixel(xx + x, yy + y, image[counter++]);
}

static void drawCentreString(const String& buf, int y, int offset = 0) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(buf, 0, y, &x1, &y1, &w, &h);
  display.setCursor(((MATRIX_WIDTH - offset) - w) / 2, y);
  display.print(buf);
}

static void drawSignalBars(int x, int y, int rssi) {
  int bars = rssi > -50 ? 4 : rssi > -60 ? 3 : rssi > -70 ? 2 : 1;
  uint16_t colors[] = { COLOR_RED, COLOR_ORANGE, COLOR_GREEN, COLOR_GREEN };
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;
    int bx = x + i * 4;
    int by = y + (8 - h);
    uint16_t c = (i < bars) ? colors[i] : rgb565(30, 30, 30);
    display.fillRect(bx, by, 3, h, c);
  }
}

static String truncSSID(const char* ssid, int maxChars = 10) {
  String s(ssid);
  if (s.length() > maxChars) s = s.substring(0, maxChars - 1) + ".";
  return s;
}

// ── Color interpolation for pollution levels ──

// PM: 4 levels
static uint16_t colorPM(float val, float s1, float s2, float s3) {
  if (val < s1) return COLOR_GREEN;
  if (val < s2) return COLOR_YELLOW;
  if (val < s3) return COLOR_ORANGE;
  return COLOR_RED;
}

static const char* msgPM(float val, float s1, float s2, float s3) {
  if (val < s1) return "Bon";
  if (val < s2) return "Moyen";
  if (val < s3) return "Degrade";
  return "Mauvais";
}

// CO2: 3 levels
static uint16_t colorCO2(int val) {
  if (val < 800)  return COLOR_GREEN;
  if (val < 1500) return COLOR_ORANGE;
  return COLOR_RED;
}

static const char* msgCO2(int val) {
  if (val < 800)  return "Bon";
  if (val < 1500) return "Aerer SVP";
  return "Mauvais";
}

// Temperature: comfort zone
static uint16_t colorTemp(float val) {
  if (val < 19) return rgb565(0, 0, 255);
  if (val < 28) return COLOR_GREEN;
  return COLOR_RED;
}

static const char* msgTemp(float val) {
  if (val < 19) return "Froid";
  if (val < 28) return "OK";
  return "Chaud";
}

// Humidity: comfort zone
static uint16_t colorHumi(float val) {
  if (val < 40) return COLOR_RED;
  if (val < 60) return COLOR_GREEN;
  return COLOR_RED;
}

static const char* msgHumi(float val) {
  if (val < 40) return "Sec";
  if (val < 60) return "Ideal";
  return "Humide";
}

// COV (TVOC): based on CCS811 ranges
static uint16_t colorCOV(int val) {
  if (val < 220)  return COLOR_GREEN;
  if (val < 660)  return COLOR_YELLOW;
  if (val < 2200) return COLOR_ORANGE;
  return COLOR_RED;
}

static const char* msgCOV(int val) {
  if (val < 220)  return "Bon";
  if (val < 660)  return "Moyen";
  if (val < 2200) return "Degrade";
  return "Mauvais";
}

// ── Generic measurement screen ──
// Layout:
//   Row 0:  Label (cyan)  +  unit (gray, small)
//   Row 9:  Value (white, large)  +  color square (14x14)
//   Row 25: Status message (colored)

static void drawMeasurementScreen(const char* label, const char* unit,
                                   const String& value, uint16_t levelColor,
                                   const char* statusMsg) {
  display.clearDisplay();

  // Label (top-left, cyan)
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(COLOR_CYAN);
  display.setCursor(1, 0);
  display.print(label);

  // Unit (after label, gray, small)
  display.setTextColor(COLOR_GRAY);
  display.setCursor(display.getCursorX() + 2, 0);
  display.print(unit);

  // Color indicator square (top-right)
  display.fillRect(50, 9, 14, 14, levelColor);

  // Value (large, centered in left 50px)
  display.setFont(NULL);
  display.setTextSize(2);
  display.setTextColor(COLOR_WHITE);
  drawCentreString(value, 9, 14);

  // Status message (bottom, colored, centered)
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(levelColor);
  drawCentreString(String(statusMsg), 25);
}

// ── Draw individual data screens ──

static void drawScreenPM25() {
  uint16_t c = colorPM(sensorCache.pm25, 10, 20, 50);
  drawMeasurementScreen("PM2.5", "ug/m3", String(sensorCache.pm25, 0), c,
                         msgPM(sensorCache.pm25, 10, 20, 50));
}

static void drawScreenPM10() {
  uint16_t c = colorPM(sensorCache.pm10, 15, 30, 75);
  drawMeasurementScreen("PM10", "ug/m3", String(sensorCache.pm10, 0), c,
                         msgPM(sensorCache.pm10, 15, 30, 75));
}

static void drawScreenCO2() {
  uint16_t c = colorCO2(sensorCache.co2);
  drawMeasurementScreen("CO2", "ppm", String(sensorCache.co2), c,
                         msgCO2(sensorCache.co2));
}

static void drawScreenTemp() {
  uint16_t c = colorTemp(sensorCache.temperature);
  drawMeasurementScreen("Temp", "C", String(sensorCache.temperature, 1), c,
                         msgTemp(sensorCache.temperature));
}

static void drawScreenHumi() {
  uint16_t c = colorHumi(sensorCache.humidity);
  drawMeasurementScreen("Humi", "%", String(sensorCache.humidity, 0), c,
                         msgHumi(sensorCache.humidity));
}

static void drawScreenCOV() {
  uint16_t c = colorCOV(sensorCache.tvoc);
  drawMeasurementScreen("COV", "ppb", String(sensorCache.tvoc), c,
                         msgCOV(sensorCache.tvoc));
}

// ── Public API ──

void displayInit() {
  Preferences prefs;
  prefs.begin("display", true);
  debugSplashEnabled = prefs.getBool("dbgSplash", false);
  prefs.end();

  display.begin(16);
  display.setDriverChip(SHIFT);
  display.setColorOrder(RRBBGG);
  display.clearDisplay();

  displaySem = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(displayTask, "display", 2048, NULL, configMAX_PRIORITIES - 1, NULL, 0);

  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &displayISR, true);
  timerAlarmWrite(timer, 4000, true);
  timerAlarmEnable(timer);

  Logger.printf("Matrix display init OK (64x32, scan 1/16, debugSplash=%s)\n",
    debugSplashEnabled ? "true" : "false");
}

void displaySetSensorData(const SensorData& data) {
  sensorCache = data;
  hasData = true;
}

void displayUpdate() {
  if (!hasData) return;

  unsigned long now = millis();
  if (now - lastScreenChange < SCREEN_INTERVAL) return;
  lastScreenChange = now;

  // Build list of available screens
  Screen avail[SCR_COUNT];
  int count = 0;
  if (sensorCache.pm_ok)  { avail[count++] = SCR_PM25; avail[count++] = SCR_PM10; }
  if (sensorCache.co2_ok) avail[count++] = SCR_CO2;
  if (sensorCache.bme_ok) { avail[count++] = SCR_TEMP; avail[count++] = SCR_HUMI; }
  if (sensorCache.ccs_ok) avail[count++] = SCR_COV;
  avail[count++] = SCR_LOGO;

  if (count == 0) return;
  currentScreen = currentScreen % count;

  switch (avail[currentScreen]) {
    case SCR_PM25:  drawScreenPM25(); break;
    case SCR_PM10:  drawScreenPM10(); break;
    case SCR_CO2:   drawScreenCO2();  break;
    case SCR_TEMP:  drawScreenTemp(); break;
    case SCR_HUMI:  drawScreenHumi(); break;
    case SCR_COV:   drawScreenCOV();  break;
    case SCR_LOGO:  displayShowLogo(); break;
    default: break;
  }

  currentScreen++;
}

void displayShowLogo() {
  display.clearDisplay();
  drawImage(0, 0, MATRIX_HEIGHT, MATRIX_WIDTH, logo_moduleair);
}

void displayShowDebugSplash() {
  display.clearDisplay();
  display.setTextColor(rgb565(0, 120, 255));
  display.setTextSize(1);
  display.setCursor(1, 8);
  display.print("ModuleAir V4");

  display.setTextColor(rgb565(100, 100, 100));
  String ver = "v" + String(FIRMWARE_VERSION);
  int verWidth = ver.length() * 6;
  display.setCursor((MATRIX_WIDTH - verWidth) / 2, 20);
  display.print(ver);

  delay(5000);
}

// ── WiFi status screens ──

static int dotCount = 0;

void displayShowWifiConnecting(const char* ssid) {
  display.clearDisplay();
  dotCount = 0;
  display.setTextSize(1);
  display.setTextColor(COLOR_BLUE);
  display.setCursor(4, 2);
  display.print("Connexion");
  display.setTextColor(COLOR_WHITE);
  display.setCursor(4, 14);
  display.print(truncSSID(ssid));
}

void displayShowWifiDots() {
  dotCount = (dotCount + 1) % 4;
  display.fillRect(4, 24, 30, 8, 0);
  display.setTextSize(1);
  display.setTextColor(COLOR_GRAY);
  display.setCursor(4, 24);
  for (int i = 0; i < dotCount; i++) display.print(".");
}

void displayShowWifiConnected(const char* ssid, int rssi) {
  display.clearDisplay();
  display.setTextSize(1);

  display.setTextColor(COLOR_GREEN);
  display.setCursor(4, 4);
  display.print("Connect");
  display.write(130);

  drawSignalBars(48, 3, rssi);

  display.setTextColor(COLOR_WHITE);
  display.setCursor(4, 18);
  display.print(truncSSID(ssid));
}

void displayShowAPMode(const char* apName, const char* apIP) {
  display.clearDisplay();
  display.setTextSize(1);

  display.setTextColor(COLOR_ORANGE);
  display.setCursor(7, 1);
  display.print("Mode  AP");

  display.setTextColor(COLOR_WHITE);
  String name(apName);
  if (name.length() > 10) name = name.substring(name.length() - 10);
  int nameWidth = name.length() * 6;
  display.setCursor((MATRIX_WIDTH - nameWidth) / 2, 13);
  display.print(name);

  display.setTextColor(COLOR_GRAY);
  display.setCursor(1, 24);
  display.print(apIP);
}

void displayShowWifiLost() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_RED);
  display.setCursor(10, 4);
  display.print("WiFi");
  display.setTextColor(COLOR_ORANGE);
  display.setCursor(4, 16);
  display.print("Deconnect");
  display.write(130);
}

void displayShowWifiReconnected() {
  hasData = false;
  currentScreen = 0;
  lastScreenChange = 0;
}

// ── OTA screens ──

void displayShowOtaUpdate() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_ORANGE);
  display.setCursor(4, 1);
  display.print("Mise a jour");
  display.setTextColor(COLOR_GRAY);
  display.setCursor(1, 13);
  display.print("Telecharg...");
  display.drawRect(2, 24, 60, 7, COLOR_GRAY);
}

void displayShowOtaProgress(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  int barWidth = (56 * percent) / 100;
  display.fillRect(4, 26, 56, 3, 0);
  if (barWidth > 0) {
    uint16_t color = percent < 50 ? COLOR_ORANGE : COLOR_GREEN;
    display.fillRect(4, 26, barWidth, 3, color);
  }
  display.fillRect(1, 13, 62, 8, 0);
  display.setTextSize(1);
  display.setTextColor(COLOR_WHITE);
  String pct = String(percent) + "%";
  int pctWidth = pct.length() * 6;
  display.setCursor((MATRIX_WIDTH - pctWidth) / 2, 13);
  display.print(pct);
}

void displayShowOtaDone() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_GREEN);
  display.setCursor(4, 4);
  display.print("Mise a jour");
  display.setTextColor(COLOR_WHITE);
  display.setCursor(7, 18);
  display.print("OK ! Reboot");
}

void displayShowOtaFailed() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_RED);
  display.setCursor(4, 4);
  display.print("Mise a jour");
  display.setTextColor(COLOR_ORANGE);
  display.setCursor(10, 18);
  display.print("Echec !");
}

// ── Preferences ──

bool displayGetDebugSplash() {
  return debugSplashEnabled;
}

void displaySetDebugSplash(bool enabled) {
  debugSplashEnabled = enabled;
  Preferences prefs;
  prefs.begin("display", false);
  prefs.putBool("dbgSplash", enabled);
  prefs.end();
}
