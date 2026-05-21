#include <Arduino.h>
#include <Preferences.h>
#include <PxMatrix.h>
#include "config.h"
#include "display.h"
#include "sensors.h"
#include "settings.h"
#include "logos.h"
#include "logger.h"
#include "fonts/Font4x7Fixed.h"

// ── Hardware ──

static uint8_t display_draw_time = 30;
static uint8_t displayBrightness = 128;
static bool debugSplashEnabled = false;
static bool refreshPaused = false;
static hw_timer_t *timer = NULL;
static SemaphoreHandle_t displaySem;

static PxMATRIX display(MATRIX_WIDTH, MATRIX_HEIGHT, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);

static void IRAM_ATTR displayISR() {
  if (refreshPaused) return;
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
enum Screen { SCR_PM1, SCR_PM25, SCR_PM10, SCR_CO2, SCR_TEMP, SCR_HUMI, SCR_COV, SCR_HCHO, SCR_LOGO_MA, SCR_LOGO_AC, SCR_LOGO_AS, SCR_COUNT };

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

// CO2: 3 levels (thresholds from settings)
static uint16_t colorCO2(int val) {
  const ThresholdsCO2& th = settingsGetThresholdsCO2();
  if (val < th.good) return COLOR_GREEN;
  if (val < th.bad)  return COLOR_ORANGE;
  return COLOR_RED;
}

static const char* msgCO2(int val) {
  const ThresholdsCO2& th = settingsGetThresholdsCO2();
  if (val < th.good) return "Bon";
  if (val < th.bad)  return "Aerer SVP";
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

static uint16_t colorHCHO(float val) {
  if (val < 10)  return COLOR_GREEN;
  if (val < 30)  return COLOR_YELLOW;
  if (val < 100) return COLOR_ORANGE;
  return COLOR_RED;
}

static const char* msgHCHO(float val) {
  if (val < 10)  return "Bon";
  if (val < 30)  return "Moyen";
  if (val < 100) return "Degrade";
  return "Mauvais";
}

// ── Generic measurement screen ──
// Layout:
//   Row 0:  Label (cyan)  +  unit (gray, small)
//   Row 9:  Value (white, large)  +  color square (14x14)
//   Row 25: Status message (colored)

// Unit IDs for special character rendering
enum UnitType { UNIT_UGM3, UNIT_PPM, UNIT_PPB, UNIT_DEGC, UNIT_PERCENT };

static void drawUnit(UnitType unit) {
  display.setFont(&Font4x7Fixed);
  display.setTextColor(COLOR_GRAY);
  switch (unit) {
    case UNIT_UGM3:
      display.write(181);  // µ
      display.print("g/m");
      display.write(179);  // ³
      break;
    case UNIT_PPM:
      display.setFont(NULL);
      display.print("ppm");
      break;
    case UNIT_PPB:
      display.setFont(NULL);
      display.print("ppb");
      break;
    case UNIT_DEGC:
      display.write(176);  // °
      display.print("C");
      break;
    case UNIT_PERCENT:
      display.print("%");
      break;
  }
  display.setFont(NULL);
}

static void drawMeasurementScreen(const char* label, UnitType unit,
                                   const String& value, uint16_t levelColor,
                                   const char* statusMsg, bool co2Label = false) {
  display.clearDisplay();

  // Label (top-left, cyan)
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(COLOR_CYAN);
  display.setCursor(1, 0);
  if (co2Label) {
    display.print("CO");
    display.write(250);  // subscript 2
  } else {
    display.print(label);
  }

  // Unit (after label, small font) — y=7 is the baseline for Font4x7Fixed (yOffset=-7).
  display.setCursor(display.getCursorX() + 2, 7);
  drawUnit(unit);

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

// Forward declarations
static void displayShowLogoAirCarto();
static void displayShowLogoAtmoSud();

static void drawScreenPM1() {
  uint16_t c = colorPM(sensorCache.pm1, 10, 20, 50);
  drawMeasurementScreen("PM1", UNIT_UGM3, String(sensorCache.pm1, 0), c,
                         msgPM(sensorCache.pm1, 10, 20, 50));
}

static void drawScreenPM25() {
  uint16_t c = colorPM(sensorCache.pm25, 10, 20, 50);
  drawMeasurementScreen("PM2.5", UNIT_UGM3, String(sensorCache.pm25, 0), c,
                         msgPM(sensorCache.pm25, 10, 20, 50));
}

static void drawScreenPM10() {
  uint16_t c = colorPM(sensorCache.pm10, 15, 30, 75);
  drawMeasurementScreen("PM10", UNIT_UGM3, String(sensorCache.pm10, 0), c,
                         msgPM(sensorCache.pm10, 15, 30, 75));
}

static void drawScreenCO2() {
  uint16_t c = colorCO2(sensorCache.co2);
  drawMeasurementScreen("CO", UNIT_PPM, String(sensorCache.co2), c,
                         msgCO2(sensorCache.co2), true);
}

static void drawScreenTemp() {
  uint16_t c = colorTemp(sensorCache.temperature);
  drawMeasurementScreen("Temp", UNIT_DEGC, String(sensorCache.temperature, 1), c,
                         msgTemp(sensorCache.temperature));
}

static void drawScreenHumi() {
  uint16_t c = colorHumi(sensorCache.humidity);
  drawMeasurementScreen("Humi", UNIT_PERCENT, String(sensorCache.humidity, 0), c,
                         msgHumi(sensorCache.humidity));
}

static void drawScreenCOV() {
  uint16_t c = colorCOV(sensorCache.tvoc);
  drawMeasurementScreen("COV", UNIT_PPB, String(sensorCache.tvoc), c,
                         msgCOV(sensorCache.tvoc));
}

static void drawScreenHCHO() {
  uint16_t c = colorHCHO(sensorCache.hcho);
  drawMeasurementScreen("HCHO", UNIT_PPB, String(sensorCache.hcho, 0), c,
                         msgHCHO(sensorCache.hcho));
}

// ── Public API ──

void displayInit() {
  Preferences prefs;
  prefs.begin("display", true);
  debugSplashEnabled = prefs.getBool("dbgSplash", false);
  displayBrightness = prefs.getUChar("brightness", 128);
  prefs.end();

  display.begin(16);
  display.setDriverChip(SHIFT);
  display.setColorOrder(RRBBGG);
  display.setBrightness(displayBrightness);
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

  // Build list of available screens: all data screens, then 1 logo (rotated each cycle)
  const ScreenSettings& scfg = settingsGetScreens();
  Screen avail[SCR_COUNT];
  int count = 0;

  // Data screens
  if (sensorCache.pm_ok) {
    if (scfg.pm1)  avail[count++] = SCR_PM1;
    if (scfg.pm25) avail[count++] = SCR_PM25;
    if (scfg.pm10) avail[count++] = SCR_PM10;
  }
  if (sensorCache.co2_ok && scfg.co2)  avail[count++] = SCR_CO2;
  if (sensorCache.bme_ok) {
    if (scfg.temp) avail[count++] = SCR_TEMP;
    if (scfg.humi) avail[count++] = SCR_HUMI;
  }
  if (sensorCache.ccs_ok && scfg.tvoc) avail[count++] = SCR_COV;
  if (sensorCache.sfa40_ok && scfg.hcho) avail[count++] = SCR_HCHO;

  // Active logos
  Screen logos[3];
  int logoCount = 0;
  if (scfg.logo_moduleair) logos[logoCount++] = SCR_LOGO_MA;
  if (scfg.logo_aircarto)  logos[logoCount++] = SCR_LOGO_AC;
  if (scfg.logo_atmosud)   logos[logoCount++] = SCR_LOGO_AS;

  // Append one logo at the end (rotated across cycles)
  static int logoRotationIdx = 0;
  if (logoCount > 0) {
    avail[count++] = logos[logoRotationIdx % logoCount];
  }

  if (count == 0) return;

  // Detect end of cycle: when we wrap back to 0, advance logo
  if (currentScreen >= count) {
    currentScreen = 0;
    if (logoCount > 0) logoRotationIdx = (logoRotationIdx + 1) % logoCount;
  }

  static const char* screenNames[] = { "PM1", "PM2.5", "PM10", "CO2", "Temp", "Humi", "COV", "HCHO", "Logo ModuleAir", "Logo AirCarto", "Logo AtmoSud" };

  Screen scr = avail[currentScreen];
  Logger.printf("[Display](%ds) Screen %d/%d: %s\n", SCREEN_INTERVAL / 1000, currentScreen + 1, count, screenNames[scr]);

  switch (scr) {
    case SCR_PM1:   drawScreenPM1();  break;
    case SCR_PM25:  drawScreenPM25(); break;
    case SCR_PM10:  drawScreenPM10(); break;
    case SCR_CO2:   drawScreenCO2();  break;
    case SCR_TEMP:  drawScreenTemp(); break;
    case SCR_HUMI:  drawScreenHumi(); break;
    case SCR_COV:   drawScreenCOV();  break;
    case SCR_HCHO:  drawScreenHCHO(); break;
    case SCR_LOGO_MA: displayShowLogo(); break;
    case SCR_LOGO_AC: displayShowLogoAirCarto(); break;
    case SCR_LOGO_AS: displayShowLogoAtmoSud(); break;
    default: break;
  }

  currentScreen++;
}

void displayShowLogo() {
  display.clearDisplay();
  drawImage(0, 0, MATRIX_HEIGHT, MATRIX_WIDTH, logo_moduleair);
}

void displayShowInterieur() {
  Logger.println("[Display] Mesure Air Interieur");
  display.clearDisplay();
  drawImage(0, 0, MATRIX_HEIGHT, MATRIX_WIDTH, interieur_no_connection);
}

static void displayShowLogoAirCarto() {
  display.clearDisplay();
  drawImage(0, 0, MATRIX_HEIGHT, MATRIX_WIDTH, logo_aircarto);
}

static void displayShowLogoAtmoSud() {
  display.clearDisplay();
  drawImage(0, 0, MATRIX_HEIGHT, MATRIX_WIDTH, logo_atmo);
}

void displayShowDebugSplash() {
  Logger.println("[Display] Debug splash (5s)");
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
  Logger.printf("[Display] WiFi connecting: %s\n", ssid);
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
  Logger.printf("[Display] WiFi connected: %s (%d dBm)\n", ssid, rssi);
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
  Logger.printf("[Display] AP mode: %s (%s)\n", apName, apIP);
  display.clearDisplay();
  display.setTextSize(1);

  display.setTextColor(COLOR_ORANGE);
  display.setCursor(4, 4);
  display.print("Config");

  display.setTextColor(COLOR_WHITE);
  display.setCursor(4, 18);
  display.print("WiFi...");
}

void displayShowWifiLost() {
  Logger.println("[Display] WiFi lost");
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

// ── Boot animation & BLE provisioning screens ──

// Force a few display refresh cycles (for use when refresh is paused)
static void manualRefresh(int cycles = 50) {
  for (int i = 0; i < cycles; i++) {
    display.display(display_draw_time);
    delayMicroseconds(200);
  }
}

void displayShowBootAnim() {
  Logger.println("[Display] Boot animation");
  refreshPaused = true;
  display.clearDisplay();

  // Circle centered on screen, radius 12
  const float cx = MATRIX_WIDTH / 2.0f - 0.5f;   // 31.5
  const float cy = MATRIX_HEIGHT / 2.0f - 0.5f;   // 15.5
  const float radius = 12.0f;
  const int stepsPerLap = 48;
  const int laps = 2;
  const int total = stepsPerLap * laps;

  // Color cycle
  const uint16_t colors[] = { COLOR_CYAN, COLOR_BLUE, COLOR_GREEN, COLOR_YELLOW, COLOR_ORANGE, COLOR_RED };
  const int nColors = 6;

  for (int i = 0; i < total; i++) {
    float angle = (float)i / stepsPerLap * 2.0f * M_PI;
    int x = (int)roundf(cx + radius * cosf(angle));
    int y = (int)roundf(cy + radius * sinf(angle));

    // Color: cycle over the laps
    float colorPos = (float)i / total * nColors;
    uint16_t c = colors[(int)colorPos % nColors];

    // Easing: very slow at edges → very fast in middle
    float t = (float)i / (total - 1);
    float ease = (1.0f - cosf(t * 2.0f * M_PI)) * 0.5f;
    ease = ease * ease;  // square for sharper contrast
    int delayMs = 1 + (int)(ease * 30);

    display.drawPixel(x, y, c);
    manualRefresh(30);
    delay(delayMs);
    display.drawPixel(x, y, 0);
  }

  display.clearDisplay();
  refreshPaused = false;
}

void displayShowBleConnected() {
  Logger.println("[Display] BLE connected");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_CYAN);
  display.setCursor(13, 4);
  display.print("BLE");
  display.setTextColor(COLOR_WHITE);
  display.setCursor(4, 18);
  display.print("Connect");
  display.write(130);
  manualRefresh(100);
}

void displayShowBleCredentials(const char* ssid) {
  Logger.printf("[Display] BLE credentials for: %s\n", ssid);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_CYAN);
  display.setCursor(1, 0);
  display.print("Identifiant");
  display.setTextColor(COLOR_ORANGE);
  display.setCursor(7, 10);
  display.print("re");
  display.write(131);
  display.print("us");
  display.setTextColor(COLOR_WHITE);
  display.setCursor(4, 22);
  display.print(truncSSID(ssid));
  manualRefresh(100);
}

void displayShowBleWifiTrying(const char* ssid) {
  Logger.printf("[Display] BLE WiFi trying: %s\n", ssid);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_BLUE);
  display.setCursor(4, 2);
  display.print("Connexion");
  display.setTextColor(COLOR_WHITE);
  display.setCursor(4, 14);
  display.print(truncSSID(ssid));
  display.setTextColor(COLOR_GRAY);
  display.setCursor(4, 24);
  display.print("...");
  manualRefresh(100);
}

void displayShowBleWifiOk(const char* ssid) {
  Logger.printf("[Display] BLE WiFi OK: %s\n", ssid);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_GREEN);
  display.setCursor(7, 4);
  display.print("WiFi OK!");
  display.setTextColor(COLOR_WHITE);
  display.setCursor(4, 18);
  display.print(truncSSID(ssid));
  manualRefresh(100);
}

void displayShowBleWifiFail() {
  Logger.println("[Display] BLE WiFi failed");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_RED);
  display.setCursor(10, 4);
  display.print("WiFi");
  display.setTextColor(COLOR_ORANGE);
  display.setCursor(7, 18);
  display.print("Echec!");
  manualRefresh(100);
}

void displayShowBleReboot() {
  Logger.println("[Display] BLE reboot");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_GREEN);
  display.setCursor(7, 4);
  display.print("Config OK");
  display.setTextColor(COLOR_WHITE);
  display.setCursor(7, 18);
  display.print("Reboot...");
  manualRefresh(100);
}

// ── OTA screens ──

void displayShowOtaUpdate() {
  Logger.println("[Display] OTA update starting");
  refreshPaused = true;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_ORANGE);
  display.setCursor(1, 0);
  display.print("Mise a jour");
  display.setTextColor(COLOR_GRAY);
  display.setCursor(1, 10);
  display.print("Telecharg...");
  display.drawRect(2, 22, 60, 7, COLOR_GRAY);
  manualRefresh(100);
}

void displayShowOtaProgress(int percent) {
  static int lastDisplayed = -1;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  // Only refresh every 10% to reduce flicker
  int rounded = (percent / 10) * 10;
  if (rounded == lastDisplayed && percent != 100) return;
  lastDisplayed = rounded;

  // Update only the percentage text (clear just that zone)
  display.fillRect(1, 10, 62, 8, 0);
  display.setTextSize(1);
  display.setTextColor(COLOR_WHITE);
  String pct = String(percent) + "%";
  int pctWidth = pct.length() * 6;
  display.setCursor((MATRIX_WIDTH - pctWidth) / 2, 10);
  display.print(pct);

  // Update only the bar fill (clear just the bar interior)
  display.fillRect(4, 24, 56, 3, 0);
  int barWidth = (56 * percent) / 100;
  if (barWidth > 0) {
    uint16_t color = percent < 50 ? COLOR_ORANGE : COLOR_GREEN;
    display.fillRect(4, 24, barWidth, 3, color);
  }

  manualRefresh(30);
}

void displayShowOtaDone() {
  Logger.println("[Display] OTA done - rebooting");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_GREEN);
  display.setCursor(1, 4);
  display.print("Mise a jour");
  display.setTextColor(COLOR_WHITE);
  display.setCursor(7, 18);
  display.print("OK! Reboot");
  manualRefresh(100);
  refreshPaused = false;
}

void displayShowOtaFailed() {
  Logger.println("[Display] OTA failed");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_RED);
  display.setCursor(1, 4);
  display.print("Mise a jour");
  display.setTextColor(COLOR_ORANGE);
  display.setCursor(13, 18);
  display.print("Echec!");
  manualRefresh(100);
  refreshPaused = false;
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

uint8_t displayGetBrightness() {
  return displayBrightness;
}

void displaySetBrightness(uint8_t brightness) {
  displayBrightness = brightness;
  display.setBrightness(brightness);
  Preferences prefs;
  prefs.begin("display", false);
  prefs.putUChar("brightness", brightness);
  prefs.end();
  Logger.printf("[Display] Brightness set to %d\n", brightness);
}
