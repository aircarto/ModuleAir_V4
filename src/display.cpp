#include <Arduino.h>
#include <PxMatrix.h>
#include "config.h"
#include "display.h"
#include "logos.h"
#include "logger.h"

static uint8_t display_draw_time = 30;
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

static void drawImage(int x, int y, int h, int w, const uint16_t image[]) {
  int counter = 0;
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++)
      display.drawPixel(xx + x, yy + y, image[counter++]);
}

void displayInit() {
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

  Logger.println("Matrix display init OK (64x32, scan 1/16)");
}

void displayUpdate() {
  // placeholder for future display state machine
}

void displayShowLogo() {
  display.clearDisplay();
  drawImage(0, 0, MATRIX_HEIGHT, MATRIX_WIDTH, logo_moduleair);
}

void displayShowDebugSplash() {
  display.clearDisplay();

  display.setTextColor(display.color565(0, 120, 255));
  display.setTextSize(1);

  // "ModuleAir V4" centre (12 chars * 6px = 72, on centre au mieux sur 64)
  display.setCursor(1, 8);
  display.print("ModuleAir V4");

  // Version en dessous
  display.setTextColor(display.color565(100, 100, 100));
  String ver = "v" + String(FIRMWARE_VERSION);
  int verWidth = ver.length() * 6;
  display.setCursor((MATRIX_WIDTH - verWidth) / 2, 20);
  display.print(ver);

  delay(5000);
}
