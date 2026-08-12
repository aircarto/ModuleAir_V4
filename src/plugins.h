#ifndef PLUGINS_H
#define PLUGINS_H

#include <Arduino.h>

// ── Plugins « info » : données externes pour les écrans horloge/météo/bourse ─
// Ces écrans complètent la rotation des polluants sur la dalle (display.cpp).
// Les toggles d'activation vivent dans ScreenSettings (settings.h, NVS
// "screens") comme n'importe quel écran ; ce module ne porte que la CONFIG
// spécifique (ville météo, ids CoinGecko), les fetchers et l'heure NTP.
//
// APIs publiques SANS CLÉ :
//   météo  = api.open-meteo.com   (10 min)
//   bourse = api.coingecko.com    (3 min)
// Persistance : NVS namespace "plugins" (Preferences), comme le reste.
//
// IMPORTANT mémoire TLS : chaque fetch ouvre son PROPRE WiFiClientSecure dans
// un bloc scopé avec setReuse(false) — même règle que data_sender.cpp (un seul
// contexte TLS vivant à la fois, ~40 Ko de heap contigu par handshake). Les
// fetchs sont appelés depuis loop(), donc jamais en même temps que l'envoi de
// données (séquentiel), et UN SEUL fetch par passage de boucle.

#define PLUGIN_CRYPTO_MAX  3

// Cadences de poll (respectent les rate-limits gratuits).
#define WEATHER_POLL   600000UL   // 10 min
#define CRYPTO_POLL    180000UL   // 3 min

struct PluginsConfig {
  String wxCity;      // libellé de la ville (géocodée côté navigateur)
  float  wxLat, wxLon;
  String cryptoIds;   // ids CoinGecko séparés par des virgules (max 4 côté UI)
};

struct WeatherData { bool valid; bool error; float temp, tmax, tmin; int code; };

struct CryptoItem { String sym; float price; float change; };
struct CryptoData { bool valid; bool error; int count; CryptoItem items[PLUGIN_CRYPTO_MAX]; };

void pluginsInit();   // charge la config NVS (setup(), après settingsInit)
void pluginsLoop();   // NTP + pollers non bloquants (loop())

PluginsConfig& pluginsCfg();
void pluginsSetWeatherPlace(const String& city, float lat, float lon);
void pluginsSetCryptoIds(const String& ids);

const WeatherData& pluginsWeather();
const CryptoData&  pluginsCrypto();

// true dès que l'heure NTP est acquise (l'écran horloge n'entre en rotation
// qu'à partir de là).
bool pluginsTimeValid();

#endif
