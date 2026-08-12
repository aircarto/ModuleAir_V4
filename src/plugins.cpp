#include "plugins.h"
#include "config.h"
#include "settings.h"
#include "logger.h"
#include <time.h>
#include <math.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ── État ────────────────────────────────────────────────────────────────────
static PluginsConfig cfg;
static WeatherData   wx;
static CryptoData    cry;
static uint32_t nextWx = 0, nextCrypto = 0;
static bool ntpStarted = false;

// ── Heure NTP ────────────────────────────────────────────────────────────────
// L'ESP32 tient l'heure en interne via SNTP ; la chaîne TZ POSIX gère le
// passage heure d'été/hiver automatiquement (contrairement à un simple offset
// fourni par le navigateur). Europe/Paris couvre la flotte actuelle.
static const char* TZ_EUROPE_PARIS = "CET-1CEST,M3.5.0,M10.5.0/3";

bool pluginsTimeValid() {
  // Avant la première synchro SNTP, time() part de l'epoch 1970 : toute valeur
  // minuscule signifie « pas encore d'heure ».
  return time(nullptr) > 100000;
}

// ── GET HTTPS court ──────────────────────────────────────────────────────────
// Client scopé + setReuse(false) : mêmes précautions mémoire que
// data_sender.cpp (le contexte mbedTLS doit être libéré à chaque appel).
static int httpsGet(const char* url, String& payload) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  if (!https.begin(client, url)) return -1;
  https.setUserAgent("ModuleAir/" FIRMWARE_VERSION);
  https.setReuse(false);
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.setConnectTimeout(6000);
  https.setTimeout(6000);
  int code = https.GET();
  if (code == 200) payload = https.getString();
  https.end();
  return code;
}

// ── Config NVS ───────────────────────────────────────────────────────────────
void pluginsInit() {
  Preferences p;
  p.begin("plugins", true);
  cfg.wxCity    = p.getString("wx_city", "Marseille");
  cfg.wxLat     = p.getString("wx_lat", "43.2965").toFloat();
  cfg.wxLon     = p.getString("wx_lon", "5.3698").toFloat();
  cfg.cryptoIds = p.getString("cg_ids", "bitcoin,ethereum,solana");
  p.end();
  wx = WeatherData();
  cry = CryptoData();
  nextWx = nextCrypto = 0;   // fetch dès que le WiFi est là (si écran activé)
  Logger.printf("[Plugins] config chargee (meteo=%s, crypto=%s)\n",
                cfg.wxCity.c_str(), cfg.cryptoIds.c_str());
}

PluginsConfig& pluginsCfg() { return cfg; }
const WeatherData& pluginsWeather() { return wx; }
const CryptoData&  pluginsCrypto()  { return cry; }

void pluginsSetWeatherPlace(const String& city, float lat, float lon) {
  cfg.wxCity = city;
  cfg.wxLat = lat;
  cfg.wxLon = lon;
  Preferences p;
  p.begin("plugins", false);
  p.putString("wx_city", cfg.wxCity);
  p.putString("wx_lat", String(cfg.wxLat, 4));
  p.putString("wx_lon", String(cfg.wxLon, 4));
  p.end();
  wx = WeatherData();          // invalide -> refetch
  nextWx = millis();
  Logger.printf("[Plugins] meteo -> %s (%.4f, %.4f)\n", city.c_str(), lat, lon);
}

void pluginsSetCryptoIds(const String& ids) {
  cfg.cryptoIds = ids;
  Preferences p;
  p.begin("plugins", false);
  p.putString("cg_ids", cfg.cryptoIds);
  p.end();
  cry = CryptoData();
  nextCrypto = millis();
  Logger.printf("[Plugins] crypto -> %s\n", ids.c_str());
}

// ── Météo — Open-Meteo (sans clé) ────────────────────────────────────────────
static void fetchWeather() {
  char url[220];
  snprintf(url, sizeof(url),
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min"
    "&timezone=auto&forecast_days=1", cfg.wxLat, cfg.wxLon);
  String pl;
  int code = httpsGet(url, pl);
  if (code != 200) { wx.error = true; Logger.printf("[Weather] code=%d\n", code); return; }
  JsonDocument doc;
  if (deserializeJson(doc, pl)) { wx.error = true; return; }
  wx.temp  = doc["current"]["temperature_2m"] | 0.0f;
  wx.code  = doc["current"]["weather_code"]   | 0;
  wx.tmax  = doc["daily"]["temperature_2m_max"][0] | wx.temp;
  wx.tmin  = doc["daily"]["temperature_2m_min"][0] | wx.temp;
  wx.valid = true; wx.error = false;
  Logger.printf("[Weather] %s %.0f\xC2\xB0\x43 code=%d\n", cfg.wxCity.c_str(), wx.temp, wx.code);
}

// ── Bourse / crypto — CoinGecko (sans clé) ───────────────────────────────────
static void fetchCrypto() {
  if (cfg.cryptoIds.isEmpty()) { cry.count = 0; cry.valid = false; cry.error = false; return; }
  String url = "https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&ids="
             + cfg.cryptoIds + "&price_change_percentage=24h";
  String pl;
  int code = httpsGet(url.c_str(), pl);
  if (code != 200) { cry.error = true; Logger.printf("[Crypto] code=%d\n", code); return; }

  JsonDocument filter;
  filter[0]["symbol"] = true;
  filter[0]["current_price"] = true;
  filter[0]["price_change_percentage_24h"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, pl, DeserializationOption::Filter(filter))) { cry.error = true; return; }

  int n = 0;
  for (JsonObject c : doc.as<JsonArray>()) {
    String sym = (const char*)(c["symbol"] | "");
    sym.toUpperCase();
    cry.items[n].sym    = sym;
    cry.items[n].price  = c["current_price"] | 0.0f;
    cry.items[n].change = c["price_change_percentage_24h"] | 0.0f;
    if (++n >= PLUGIN_CRYPTO_MAX) break;
  }
  cry.count = n; cry.valid = n > 0; cry.error = (n == 0);
  Logger.printf("[Crypto] %d actifs\n", n);
}

// ── Boucle : NTP + poll non bloquant ─────────────────────────────────────────
void pluginsLoop() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!ntpStarted) {
    configTzTime(TZ_EUROPE_PARIS, "pool.ntp.org", "time.google.com");
    ntpStarted = true;
    Logger.println("[Plugins] NTP demarre (Europe/Paris)");
  }

  // Un seul fetch (= un seul contexte TLS) par passage de boucle : les écrans
  // désactivés ne coûtent aucune requête.
  const ScreenSettings& scfg = settingsGetScreens();
  uint32_t now = millis();
  if (scfg.weather && (int32_t)(now - nextWx) >= 0) {
    fetchWeather();
    nextWx = now + (wx.valid ? WEATHER_POLL : 60000UL);   // erreur -> retry 1 min
  } else if (scfg.crypto && (int32_t)(now - nextCrypto) >= 0) {
    fetchCrypto();
    nextCrypto = now + (cry.valid ? CRYPTO_POLL : 60000UL);
  }
}
