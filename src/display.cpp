#include <Arduino.h>
#include <Preferences.h>
#include <PxMatrix.h>
#include "config.h"
#include "display.h"
#include "sensors.h"
#include "settings.h"
#include "logos.h"
#include "logo_storage.h"
#include "logger.h"
#include "fonts/Font4x7Fixed.h"
#include "wifi_icon.h"

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
enum Screen { SCR_PM1, SCR_PM25, SCR_PM10, SCR_CO2, SCR_TEMP, SCR_HUMI, SCR_COV, SCR_HCHO,
              SCR_PM_ERR, SCR_CO2_ERR,
              SCR_LOGO_MA, SCR_LOGO_AC, SCR_LOGO_AS, SCR_COUNT };

// ── Helpers ──

static void drawImage(int x, int y, int h, int w, const uint16_t image[]) {
  int counter = 0;
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++)
      display.drawPixel(xx + x, yy + y, image[counter++]);
}

// Like drawImage but treats 0x0000 pixels as transparent (skip the draw).
// Lets the WiFi icon overlay text or coexist with other drawing without
// erasing the underlying pixels.
static void drawImageTransparent(int x, int y, int h, int w, const uint16_t image[]) {
  int counter = 0;
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++) {
      uint16_t px = image[counter++];
      if (px) display.drawPixel(xx + x, yy + y, px);
    }
}

static int wifiFrameIdx = 0;
static const int WIFI_ICON_X = 47;
static const int WIFI_ICON_Y = 15;

static void drawWifiIcon(int frame) {
  display.fillRect(WIFI_ICON_X, WIFI_ICON_Y, WIFI_ICON_W, WIFI_ICON_H, 0);
  drawImageTransparent(WIFI_ICON_X, WIFI_ICON_Y, WIFI_ICON_H, WIFI_ICON_W,
                       wifiFrames[frame % WIFI_ICON_FRAMES]);
}

// ── Tiny network-status badge for measurement screens ──
// 8-wide monochrome icons, one byte per row, MSB = leftmost pixel.

// Upward WiFi, compressed to 6 rows so every strata is one row apart in the
// center column (lit / empty / lit / empty / lit / lit), instead of two empty
// rows separating the apexes. Each arc is still drawn as a 1-pixel-thick
// diagonal outline so the curve reads as a curve, not a chunky bar.
//   y0: ..####..   outer apex
//   y1: .#....#.   outer arc descending diagonally
//   y2: ...##...   middle apex (directly under the outer arc)
//   y3: ..#..#..   middle arc descending diagonally
//   y4: ...##...   2×2 square at the base, row 1
//   y5: ...##...   square row 2
static const uint8_t iconNetOK[6]         = { 0x3C, 0x42, 0x18, 0x24, 0x18, 0x18 };
// Same arcs without the central square → "broken" signal, drawn in red.
static const uint8_t iconNetNoInternet[6] = { 0x3C, 0x42, 0x18, 0x24, 0x00, 0x00 };
// Up + down arrows side by side (red — server unreachable / API failed):
//   ........   .#...#..   ###..#..   .#...#..   .#..###.   .#...#..   ........
static const uint8_t iconNetApiError[7]   = { 0x00, 0x44, 0xE4, 0x44, 0x4E, 0x44, 0x00 };

static NetStatus netStatus = NET_OK;

void displaySetNetStatus(NetStatus s) {
  netStatus = s;
}

static void drawMonoIcon(int x, int y, int w, int h, const uint8_t* bitmap, uint16_t color) {
  for (int row = 0; row < h; row++) {
    uint8_t b = bitmap[row];
    for (int col = 0; col < w; col++) {
      if (b & (0x80 >> col)) display.drawPixel(x + col, y + row, color);
    }
  }
}

static void drawNetStatusBadge() {
  const int x = 55, y = 0;
  switch (netStatus) {
    case NET_OK:           drawMonoIcon(x, y, 8, 6, iconNetOK,         COLOR_BLUE); break;
    case NET_NO_INTERNET:  drawMonoIcon(x, y, 8, 6, iconNetNoInternet, COLOR_RED);  break;
    case NET_API_ERROR:    drawMonoIcon(x, y, 8, 7, iconNetApiError,   COLOR_RED);  break;
  }
}

static void drawCentreString(const String& buf, int y, int offset = 0) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(buf, 0, y, &x1, &y1, &w, &h);
  display.setCursor(((MATRIX_WIDTH - offset) - w) / 2, y);
  display.print(buf);
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

  // Unit: drawUnit() will switchFont(NULL → Font4x7Fixed) which auto-shifts
  // cursor.y by +6 (Adafruit_GFX::setFont). y=1 here → baseline ends up at 7,
  // putting tall glyphs (h=7, yo=-7) at rows 0-6, top-aligned with the label.
  display.setCursor(display.getCursorX() + 2, 1);
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

  // Connectivity badge top-right (above the color square)
  drawNetStatusBadge();
}

// ── Warning triangle (used by error screens) ──
// 14x14 orange filled triangle with a centered "!". Drawn at (x,y).
// Sits exactly where the measurement screens put their color square,
// so error screens reuse the same visual rhythm.
static void drawWarningIcon(int x, int y) {
  // Triangle plein orange (pointe en haut, base en bas)
  display.fillTriangle(
    x + 7,  y,        // sommet (haut centre)
    x,      y + 13,   // bas gauche
    x + 13, y + 13,   // bas droit
    COLOR_ORANGE);
  // "!" blanc au centre — barre 2px de large
  display.fillRect(x + 6, y + 4, 2, 5, COLOR_WHITE);
  // Point du "!"
  display.fillRect(x + 6, y + 10, 2, 2, COLOR_WHITE);
}

// ── Generic error screen ──
// Same layout grammar as drawMeasurementScreen:
//   - top-left "Capteur" (cyan)        ← in place of the metric label
//   - centered sensor name (white, x2) ← in place of the value
//   - 14x14 warning triangle           ← in place of the color square
//   - bottom "Erreur" (red)            ← in place of the status message
//   - top-right network badge          ← unchanged
static void drawErrorScreen(const String& sensorName) {
  display.clearDisplay();

  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(COLOR_CYAN);
  display.setCursor(1, 0);
  display.print("Capteur");

  // Triangle d'attention à la place du carré couleur (mêmes coordonnées)
  drawWarningIcon(50, 9);

  // Nom du capteur en grand, centré sur la zone gauche (laisse 14px pour le triangle)
  display.setFont(NULL);
  display.setTextSize(2);
  display.setTextColor(COLOR_WHITE);
  drawCentreString(sensorName, 9, 14);

  // "Erreur" en rouge tout en bas, centré
  display.setTextSize(1);
  display.setTextColor(COLOR_RED);
  drawCentreString(String("Erreur"), 25);

  drawNetStatusBadge();
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

static void drawScreenPMError()  { drawErrorScreen("PM");  }
static void drawScreenCO2Error() { drawErrorScreen("CO2"); }

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
  const SensorSettings& sensCfg = settingsGetSensors();
  Screen avail[SCR_COUNT];
  int count = 0;

  // Data screens. We distinguish three states per family:
  //   1. Sensor enabled + reading OK    -> show data screens
  //   2. Sensor enabled + reading KO    -> show single error screen
  //   3. Sensor disabled by user        -> hide everything (NOT an error)
  if (sensorCache.pm_ok) {
    if (scfg.pm1)  avail[count++] = SCR_PM1;
    if (scfg.pm25) avail[count++] = SCR_PM25;
    if (scfg.pm10) avail[count++] = SCR_PM10;
  } else if (sensCfg.npm_enabled && (scfg.pm1 || scfg.pm25 || scfg.pm10)) {
    avail[count++] = SCR_PM_ERR;
  }
  if (scfg.co2) {
    if (sensorCache.co2_ok) {
      avail[count++] = SCR_CO2;
    } else if (sensCfg.mhz19_enabled) {
      avail[count++] = SCR_CO2_ERR;
    }
  }
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

  static const char* screenNames[] = { "PM1", "PM2.5", "PM10", "CO2", "Temp", "Humi", "COV", "HCHO",
                                       "PM ERR", "CO2 ERR",
                                       "Logo ModuleAir", "Logo AirCarto", "Logo AtmoSud" };

  Screen scr = avail[currentScreen];
  Logger.printf("[Display](%ds) Screen %d/%d: %s\n", SCREEN_INTERVAL / 1000, currentScreen + 1, count, screenNames[scr]);

  switch (scr) {
    case SCR_PM1:     drawScreenPM1();      break;
    case SCR_PM25:    drawScreenPM25();     break;
    case SCR_PM10:    drawScreenPM10();     break;
    case SCR_CO2:     drawScreenCO2();      break;
    case SCR_TEMP:    drawScreenTemp();     break;
    case SCR_HUMI:    drawScreenHumi();     break;
    case SCR_COV:     drawScreenCOV();      break;
    case SCR_HCHO:    drawScreenHCHO();     break;
    case SCR_PM_ERR:  drawScreenPMError();  break;
    case SCR_CO2_ERR: drawScreenCO2Error(); break;
    case SCR_LOGO_MA: displayShowLogo();           break;
    case SCR_LOGO_AC: displayShowLogoAirCarto();   break;
    case SCR_LOGO_AS: displayShowLogoAtmoSud();    break;
    default: break;
  }

  currentScreen++;
}

void displayShowLogo() {
  display.clearDisplay();
  drawImage(0, 0, MATRIX_HEIGHT, MATRIX_WIDTH, logoBufferGet(LOGO_SLOT_MA));
}

void displayShowInterieur() {
  Logger.println("[Display] Mesure Air Interieur");
  display.clearDisplay();
  drawImage(0, 0, MATRIX_HEIGHT, MATRIX_WIDTH, interieur_no_connection);
}

static void displayShowLogoAirCarto() {
  display.clearDisplay();
  drawImage(0, 0, MATRIX_HEIGHT, MATRIX_WIDTH, logoBufferGet(LOGO_SLOT_AC));
}

static void displayShowLogoAtmoSud() {
  display.clearDisplay();
  drawImage(0, 0, MATRIX_HEIGHT, MATRIX_WIDTH, logoBufferGet(LOGO_SLOT_AS));
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
// V2.1-style layout: 3 lines of text on the left (y=0/11/22) +
// animated 16x16 WiFi icon on the right at (47, 15).

// Minimum on-screen time for the "Connexion" screen — keeps it readable
// even when the AP accepts our STA almost immediately.
static const unsigned long WIFI_CONNECTING_MIN_MS = 1800;
static unsigned long wifiConnectingStartMs = 0;

void displayShowWifiConnecting(const char* ssid) {
  Logger.printf("[Display] WiFi connecting: %s\n", ssid);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_CYAN);
  display.setCursor(1, 0);
  display.print("Connexion");
  display.setTextColor(COLOR_WHITE);
  display.setCursor(1, 11);
  display.print(truncSSID(ssid, 7));
  wifiFrameIdx = 0;
  drawWifiIcon(0);
  wifiConnectingStartMs = millis();
}

void displayShowWifiDots() {
  wifiFrameIdx = (wifiFrameIdx + 1) % WIFI_ICON_FRAMES;
  drawWifiIcon(wifiFrameIdx);
}

void displayShowWifiConnected(const char* ssid, int rssi) {
  // If "Connexion" was just shown, hold it (with the icon still spinning)
  // until WIFI_CONNECTING_MIN_MS has elapsed since it appeared.
  if (wifiConnectingStartMs) {
    unsigned long elapsed = millis() - wifiConnectingStartMs;
    while (elapsed < WIFI_CONNECTING_MIN_MS) {
      displayShowWifiDots();
      delay(150);
      elapsed = millis() - wifiConnectingStartMs;
    }
    wifiConnectingStartMs = 0;
  }

  Logger.printf("[Display] WiFi connected: %s (%d dBm)\n", ssid, rssi);
  display.clearDisplay();
  display.setTextSize(1);

  display.setTextColor(COLOR_GREEN);
  display.setCursor(1, 0);
  display.print("Connect");
  display.write(130);  // é

  int quality = constrain(2 * (rssi + 100), 0, 100);
  display.setTextColor(COLOR_WHITE);
  display.setCursor(1, 11);
  display.print(quality);
  display.print("%");

  display.setTextColor(COLOR_GRAY);
  display.setCursor(1, 22);
  display.print(truncSSID(ssid, 7));

  drawWifiIcon(WIFI_ICON_FRAMES - 1);
}

void displayShowAPMode(const char* apName, const char* apIP) {
  Logger.printf("[Display] AP mode: %s (%s)\n", apName, apIP);
  display.clearDisplay();
  display.setTextSize(1);

  display.setTextColor(COLOR_ORANGE);
  display.setCursor(1, 0);
  display.print("Config");

  display.setTextColor(COLOR_WHITE);
  display.setCursor(1, 11);
  display.print("WiFi");

  display.setTextColor(COLOR_GRAY);
  display.setCursor(1, 22);
  display.print("3 min.");

  drawWifiIcon(WIFI_ICON_FRAMES - 1);
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

// Anti-aliased rotating spinner using bilinear sub-pixel splatting:
// each trail point lives at a float (fx, fy) and distributes its brightness
// across the 4 surrounding integer pixels weighted by (1-dx)(1-dy) etc.
// Accumulating into a brightness buffer means overlapping trail points
// blend properly, and the circular path looks smooth instead of polygonal
// (the usual artifact of plain round() at low resolution).
void displayShowBootAnim() {
  Logger.println("[Display] Boot animation");
  display.clearDisplay();

  const float cx = MATRIX_WIDTH / 2.0f - 0.5f;
  const float cy = MATRIX_HEIGHT / 2.0f - 0.5f;
  const float radius = 13.0f;          // bigger circle (diameter 26 on a 32-tall matrix)
  const int tailLen = 24;              // tail still covers 120° of arc
  const int stepsPerLap = 72;          // 5° per step (unchanged → same rotation speed)
  const int spinLaps = 1;              // one full spin lap (was 2) → shorter animation
  const int spinFrames = stepsPerLap * spinLaps;
  const int fadeFrames = stepsPerLap;  // one extra lap fades the whole spinner out
  const int totalFrames = spinFrames + fadeFrames;
  const float angleStep = (2.0f * (float)M_PI) / stepsPerLap;
  const int frameDelayMs = 28;         // ≈36 fps; total ≈4 s

  // Cubic fade for a clearly defined head + long soft tail.
  uint8_t tailBright[tailLen];
  for (int i = 0; i < tailLen; i++) {
    float t = 1.0f - (float)i / tailLen;
    tailBright[i] = (uint8_t)(t * t * t * 255);
  }

  // Brightness accumulator centered on the matrix middle. uint16_t gives
  // headroom when several trail points splat onto the same pixel.
  static const int BS = 32;
  static uint16_t spinBuf[BS * BS];
  const int bx0 = (int)floorf(cx - BS / 2.0f);
  const int by0 = (int)floorf(cy - BS / 2.0f);

  for (int f = 0; f < totalFrames; f++) {
    memset(spinBuf, 0, sizeof(spinBuf));

    float fadeOut = 1.0f;
    if (f >= spinFrames) {
      float t = (float)(f - spinFrames) / fadeFrames;
      fadeOut = (1.0f - t) * (1.0f - t);
    }

    float headAngle = (float)f * angleStep - (float)M_PI / 2.0f;

    for (int i = 0; i < tailLen; i++) {
      float a = headAngle - (float)i * angleStep;
      float fx = cx + radius * cosf(a);
      float fy = cy + radius * sinf(a);
      float B = (float)tailBright[i] * fadeOut;
      if (B < 1.0f) continue;

      int x0 = (int)floorf(fx);
      int y0 = (int)floorf(fy);
      float dx = fx - x0;
      float dy = fy - y0;

      float w[4] = {
        (1.0f - dx) * (1.0f - dy),
        dx * (1.0f - dy),
        (1.0f - dx) * dy,
        dx * dy
      };
      const int ox[4] = { 0, 1, 0, 1 };
      const int oy[4] = { 0, 0, 1, 1 };

      for (int k = 0; k < 4; k++) {
        int px = x0 + ox[k] - bx0;
        int py = y0 + oy[k] - by0;
        if (px >= 0 && px < BS && py >= 0 && py < BS) {
          uint32_t acc = (uint32_t)spinBuf[py * BS + px] + (uint32_t)(B * w[k]);
          spinBuf[py * BS + px] = (uint16_t)(acc > 255 ? 255 : acc);
        }
      }
    }

    display.clearDisplay();
    for (int by = 0; by < BS; by++) {
      for (int bx = 0; bx < BS; bx++) {
        uint8_t b = (uint8_t)spinBuf[by * BS + bx];
        if (b) display.drawPixel(bx + bx0, by + by0, display.color565(b, b, b));
      }
    }
    delay(frameDelayMs);
  }

  display.clearDisplay();
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

// During OTA, leave the ISR-driven refresh running so the matrix stays lit
// across the whole download — pausing it (as the old code did) blacked out
// the screen between progress ticks because HUB75 LEDs need constant scan.
// Framebuffer updates here are picked up by the next ISR tick (~4 ms).
//
// Layout (64x32 matrix) during download:
//
//   ┌─────────────────────────────────────────────────────┐
//   │ Mise a              ░▓█░                            │  y=0-7
//   │ jour                ▓███▓                           │  y=8-15
//   │                      ░█░                            │  y=16-23
//   │ 42 %                                                │  y=22-29
//   └─────────────────────────────────────────────────────┘
//      <─── text zone ───>  <──── spinner zone ────>
//        x=0..43               x=44..63
//
// The spinner is a tiny anti-aliased version of the boot animation drawn
// every progress callback, so the user sees continuous motion even when
// the percentage is stuck for a while (slow download). The percentage
// text is only redrawn when it actually changes value, which eliminates
// the flicker the old fillRect-the-whole-zone approach caused.

// Anti-aliased OTA spinner. Frame counter advances once per call so the
// rotation reflects download activity — if the callback stops firing,
// the spinner freezes, which is itself a useful "OTA stalled" hint.
static void drawOtaSpinner() {
  static int frame = 0;
  frame++;

  const float cx = 53.0f, cy = 12.0f;
  const float radius = 5.0f;
  const int tailLen = 14;
  const int stepsPerLap = 36;  // 10 deg per step
  const float angleStep = (2.0f * (float)M_PI) / stepsPerLap;
  const int BS = 14;
  static uint16_t spinBuf[14 * 14];
  memset(spinBuf, 0, sizeof(spinBuf));
  const int bx0 = (int)floorf(cx - BS / 2.0f);
  const int by0 = (int)floorf(cy - BS / 2.0f);

  uint8_t tailBright[14];
  for (int i = 0; i < tailLen; i++) {
    float t = 1.0f - (float)i / tailLen;
    tailBright[i] = (uint8_t)(t * t * t * 255);
  }

  float headAngle = (float)frame * angleStep - (float)M_PI / 2.0f;
  for (int i = 0; i < tailLen; i++) {
    float a = headAngle - (float)i * angleStep;
    float fx = cx + radius * cosf(a);
    float fy = cy + radius * sinf(a);
    float B = (float)tailBright[i];
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float dx = fx - x0, dy = fy - y0;
    float w[4] = { (1 - dx) * (1 - dy), dx * (1 - dy), (1 - dx) * dy, dx * dy };
    const int ox[4] = { 0, 1, 0, 1 };
    const int oy[4] = { 0, 0, 1, 1 };
    for (int k = 0; k < 4; k++) {
      int px = x0 + ox[k] - bx0, py = y0 + oy[k] - by0;
      if (px >= 0 && px < BS && py >= 0 && py < BS) {
        uint32_t acc = spinBuf[py * BS + px] + (uint32_t)(B * w[k]);
        spinBuf[py * BS + px] = (uint16_t)(acc > 255 ? 255 : acc);
      }
    }
  }

  // Blit the accumulator to the framebuffer, tinted orange (r=255, g=140, b=0).
  // Clear-and-redraw is atomic-enough at this small size that the ISR rarely
  // catches a torn frame; if it does, the brightness accumulation makes it
  // visually negligible.
  for (int by = 0; by < BS; by++) {
    for (int bx = 0; bx < BS; bx++) {
      uint8_t b = (uint8_t)spinBuf[by * BS + bx];
      uint8_t r  = b;
      uint8_t g  = (uint8_t)((b * 140u) / 255u);
      display.drawPixel(bx + bx0, by + by0, display.color565(r, g, 0));
    }
  }
}

void displayShowOtaUpdate() {
  Logger.println("[Display] OTA update starting");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_ORANGE);
  display.setCursor(1, 0);
  display.print("Mise a");
  display.setCursor(1, 10);
  display.print("jour");
  display.setTextColor(COLOR_WHITE);
  display.setCursor(1, 22);
  display.print("--%");
  drawOtaSpinner();
}

void displayShowOtaProgress(int percent) {
  static int lastPct = -2;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;

  // Always advance the spinner — its job is to communicate "still working".
  drawOtaSpinner();

  // Only redraw the percentage text when the displayed value actually
  // changes. Throttle to 1% boundaries: enough for smooth perceived
  // progress without thrashing the framebuffer (which can starve the
  // SPI flash writer during OTA).
  if (percent == lastPct) return;
  lastPct = percent;

  display.fillRect(0, 22, 44, 8, 0);
  display.setTextColor(COLOR_WHITE);
  display.setCursor(1, 22);
  display.print(percent);
  display.print(" %");
}

// Helper: print a string horizontally centered on the matrix at the given y.
// Default GFX font is 6 px per char (5 glyph + 1 spacing), so width = 6*len.
static void printCentered(const char* s, int y) {
  int width = (int)strlen(s) * 6;
  int x = (MATRIX_WIDTH - width) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(s);
}

void displayShowOtaDone() {
  Logger.println("[Display] OTA done - rebooting");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_GREEN);
  printCentered("Mise a",   1);   // 6 chars  -> x=14
  printCentered("reussie!", 11);  // 8 chars  -> x=8
  display.setTextColor(COLOR_WHITE);
  printCentered("Reboot...", 22); // 9 chars  -> x=5
}

void displayShowOtaFailed() {
  Logger.println("[Display] OTA failed");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(COLOR_RED);
  printCentered("Mise a",  1);   // 6 chars -> x=14
  printCentered("jour KO", 11);  // 7 chars -> x=11
  display.setTextColor(COLOR_GRAY);
  printCentered("Voir logs", 22); // 9 chars -> x=5
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
