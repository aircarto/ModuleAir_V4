#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include "config.h"
#include "led.h"
#include "logger.h"

static Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
static Preferences ledPrefs;

enum LedMode { LED_STARTUP, LED_BREATHING_AP, LED_CONNECTED_ANIM, LED_OFF, LED_BREATHING, LED_PULSE, LED_NO_INTERNET, LED_SERVER_DOWN, LED_WIFI_LOST };
static LedMode ledMode = LED_STARTUP;
static unsigned long ledLastUpdate = 0;
static int ledStep = 0;
static unsigned long animStart = 0;
static bool ledsEnabled = false;
static uint8_t ledBrightness = 50;

#define PULSE_DURATION_MS 800
#define CONNECTED_ANIM_MS 5000

// ── Startup : chenillard arc-en-ciel + clignotements blancs ──

static void ledUpdateStartup() {
  unsigned long now = millis();
  if (now - ledLastUpdate < 150) return;
  ledLastUpdate = now;

  if (ledStep < LED_COUNT) {
    uint32_t color = strip.ColorHSV(ledStep * 65536L / LED_COUNT);
    strip.setPixelColor(ledStep, strip.gamma32(color));
    strip.show();
    ledStep++;
  } else if (ledStep < LED_COUNT + 6) {
    int phase = ledStep - LED_COUNT;
    if (phase % 2 == 0) {
      strip.fill(strip.Color(50, 50, 50));
    } else {
      strip.clear();
    }
    strip.show();
    ledStep++;
  } else {
    strip.clear();
    strip.show();
    ledMode = LED_BREATHING_AP;
    ledStep = 0;
    ledLastUpdate = now;
  }
}

// ── Mode AP : respiration orange/rouge douce ──

static void ledUpdateBreathingAP() {
  unsigned long now = millis();
  if (now - ledLastUpdate < 30) return;
  ledLastUpdate = now;

  ledStep = (ledStep + 1) % 360;
  float angle = ledStep * 3.14159 / 180.0;
  float breath = sin(angle) * sin(angle);
  uint8_t b = (uint8_t)(breath * 40);

  strip.fill(strip.Color(b, b / 3, 0));
  strip.show();
}

// ── Animation connexion WiFi réussie : clignotements bleus francs 5s ──

static void ledUpdateConnectedAnim() {
  unsigned long now = millis();
  unsigned long elapsed = now - animStart;

  if (elapsed >= CONNECTED_ANIM_MS) {
    if (ledsEnabled) {
      ledMode = LED_BREATHING;
    } else {
      strip.clear();
      strip.show();
      ledMode = LED_OFF;
    }
    ledStep = 0;
    return;
  }

  if (now - ledLastUpdate < 200) return;
  ledLastUpdate = now;

  ledStep = !ledStep;
  if (ledStep) {
    strip.fill(strip.Color(0, 30, 80));  // bleu
  } else {
    strip.clear();
  }
  strip.show();
}

// ── Respiration bleue continue (mode connecté) ──

static void ledUpdateBreathing() {
  unsigned long now = millis();
  if (now - ledLastUpdate < 30) return;
  ledLastUpdate = now;

  ledStep = (ledStep + 1) % 360;
  float angle = ledStep * 3.14159 / 180.0;
  float breath = sin(angle) * sin(angle);
  uint8_t b = (uint8_t)(breath * 40);

  strip.fill(strip.Color(0, b / 3, b));  // bleu
  strip.show();
}

// ── Pulse rapide pendant l'envoi de données (2s) ──

#define PULSE_DURATION_MS 2000

static void ledUpdatePulse() {
  unsigned long now = millis();
  unsigned long elapsed = now - animStart;

  if (elapsed >= PULSE_DURATION_MS) {
    ledMode = LED_BREATHING;
    ledStep = 0;
    return;
  }

  if (now - ledLastUpdate < 100) return;
  ledLastUpdate = now;

  ledStep = !ledStep;
  if (ledStep) {
    strip.fill(strip.Color(10, 50, 120));  // bleu vif
  } else {
    strip.fill(strip.Color(0, 5, 15));     // bleu très faible
  }
  strip.show();
}

// ── Damier orange : pas d'internet (1 LED sur 2, alternance) ──

static void ledUpdateNoInternet() {
  unsigned long now = millis();
  if (now - ledLastUpdate < 800) return;
  ledLastUpdate = now;

  ledStep = !ledStep;
  for (int i = 0; i < LED_COUNT; i++) {
    if ((i % 2) == ledStep) {
      strip.setPixelColor(i, strip.Color(40, 13, 0));  // orange
    } else {
      strip.setPixelColor(i, 0);
    }
  }
  strip.show();
}

// ── Damier violet : serveur down (1 LED sur 2, alternance) ──

static void ledUpdateServerDown() {
  unsigned long now = millis();
  if (now - ledLastUpdate < 800) return;
  ledLastUpdate = now;

  ledStep = !ledStep;
  for (int i = 0; i < LED_COUNT; i++) {
    if ((i % 2) == ledStep) {
      strip.setPixelColor(i, strip.Color(30, 0, 40));  // violet
    } else {
      strip.setPixelColor(i, 0);
    }
  }
  strip.show();
}

// ── Damier rouge : WiFi deconnecte (1 LED sur 2, alternance) ──

static void ledUpdateWifiLost() {
  unsigned long now = millis();
  if (now - ledLastUpdate < 800) return;
  ledLastUpdate = now;

  ledStep = !ledStep;
  for (int i = 0; i < LED_COUNT; i++) {
    if ((i % 2) == ledStep) {
      strip.setPixelColor(i, strip.Color(40, 0, 0));  // rouge
    } else {
      strip.setPixelColor(i, 0);
    }
  }
  strip.show();
}

// ── API publique ──

void ledInit() {
  strip.begin();

  // Charger les préférences depuis NVS
  ledPrefs.begin("leds", true);
  ledsEnabled = ledPrefs.getBool("enabled", false);
  ledBrightness = ledPrefs.getUChar("brightness", 50);
  ledPrefs.end();

  strip.setBrightness(ledBrightness);
  strip.clear();
  strip.show();
  Logger.printf("LEDs init OK (enabled=%s, brightness=%d)\n",
    ledsEnabled ? "true" : "false", ledBrightness);
}

void ledUpdate() {
  switch (ledMode) {
    case LED_STARTUP:        ledUpdateStartup();        break;
    case LED_BREATHING_AP:   ledUpdateBreathingAP();    break;
    case LED_CONNECTED_ANIM: ledUpdateConnectedAnim();  break;
    case LED_BREATHING:      ledUpdateBreathing();      break;
    case LED_PULSE:          ledUpdatePulse();          break;
    case LED_NO_INTERNET:    ledUpdateNoInternet();     break;
    case LED_SERVER_DOWN:    ledUpdateServerDown();     break;
    case LED_WIFI_LOST:      ledUpdateWifiLost();       break;
    case LED_OFF:            break;
  }
}

void ledTriggerPulse() {
  if (!ledsEnabled) return;
  ledMode = LED_PULSE;
  animStart = millis();
  ledStep = 0;
}

void ledSetApMode(bool ap) {
  if (ap) {
    ledMode = LED_BREATHING_AP;
    ledStep = 0;
  }
}

void ledStartConnectedAnim() {
  ledMode = LED_CONNECTED_ANIM;
  animStart = millis();
  ledStep = 0;
}

static void ledResetToNormal() {
  if (ledsEnabled) {
    ledMode = LED_BREATHING;
  } else {
    strip.clear();
    strip.show();
    ledMode = LED_OFF;
  }
  ledStep = 0;
}

void ledSetNoInternet(bool noInternet) {
  if (noInternet) {
    ledMode = LED_NO_INTERNET;
    ledStep = 0;
  } else if (ledMode == LED_NO_INTERNET) {
    ledResetToNormal();
  }
}

void ledSetServerDown(bool serverDown) {
  if (serverDown) {
    ledMode = LED_SERVER_DOWN;
    ledStep = 0;
  } else if (ledMode == LED_SERVER_DOWN) {
    ledResetToNormal();
  }
}

void ledSetWifiLost(bool lost) {
  if (lost) {
    ledMode = LED_WIFI_LOST;
    ledStep = 0;
  } else if (ledMode == LED_WIFI_LOST) {
    ledResetToNormal();
  }
}

void ledSetEnabled(bool enabled) {
  ledsEnabled = enabled;
  ledPrefs.begin("leds", false);
  ledPrefs.putBool("enabled", enabled);
  ledPrefs.end();

  if (enabled && (ledMode == LED_OFF)) {
    ledMode = LED_BREATHING;
    ledStep = 0;
  } else if (!enabled && ledMode != LED_STARTUP && ledMode != LED_CONNECTED_ANIM) {
    strip.clear();
    strip.show();
    ledMode = LED_OFF;
  }
}

bool ledIsEnabled() {
  return ledsEnabled;
}

void ledSetBrightness(uint8_t brightness) {
  ledBrightness = brightness;
  strip.setBrightness(brightness);
  strip.show();

  ledPrefs.begin("leds", false);
  ledPrefs.putUChar("brightness", brightness);
  ledPrefs.end();
}

uint8_t ledGetBrightness() {
  return ledBrightness;
}
