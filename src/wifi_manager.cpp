#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>
#include "config.h"
#include "wifi_manager.h"
#include "ble_improv.h"
#include "sensors.h"
#include "display.h"
#include "settings.h"
#include "data_sender.h"
#include "logger.h"
#include "i18n.h"
#include "plugins.h"
#include "web_index.h"   // SPA gzippée (générée depuis web/app.html par web_embed.py)
#include <ArduinoJson.h>

static WebServer server(80);
static DNSServer dnsServer;
static Preferences preferences;

// ── WiFi connection state machine ──────────────────────────────────────────
// Modeled on ESPHome's wifi_component FSM (STATE_AP / STATE_STA_CONNECTING /
// STATE_STA_CONNECTED) but specialized for our UX needs:
//
//   WS_STA_CONNECTED  : connected to a known WiFi, normal operation
//   WS_AP_CONFIG      : boot connect failed, AP up, matrix shows "Config WiFi"
//                       splash (first 3 minutes after entering this state)
//   WS_AP_DATA        : 3+ minutes of AP-only, matrix switches to normal
//                       pollutant data display, hotspot stays running in
//                       background so the user can still reconfigure
//   WS_AP_RETRYING    : AP+STA dual mode, attempting a background reconnect
//                       to the saved credentials (no reboot needed if it works)
enum WifiState {
  WS_STA_CONNECTED,
  WS_STA_RECONNECTING,   // STA dropped: trying to reconnect for up to 3 min
  WS_AP_CONFIG,
  WS_AP_DATA,
  WS_AP_RETRYING,
};

static WifiState wifiState = WS_AP_CONFIG;
static unsigned long stateEnteredAt = 0;
static unsigned long lastReconnectKickAt = 0;   // last manual WiFi.reconnect() in STA_RECONNECTING
static int lastAPClients = 0;

// Timings for the connection-recovery UX.
static const unsigned long STA_RECONNECT_WINDOW_MS = 3UL  * 60UL * 1000UL;  // 3 min STA recovery
static const unsigned long STA_RECONNECT_KICK_MS   = 30UL * 1000UL;         // explicit re-kick cadence
static const unsigned long AP_CONFIG_DURATION_MS   = 3UL  * 60UL * 1000UL;  // 3 min config splash
static const unsigned long AP_RETRY_INTERVAL_MS    = 10UL * 60UL * 1000UL;  // retry every 10 min
static const unsigned long AP_RETRY_TIMEOUT_MS     = 30UL * 1000UL;         // 30s per attempt

static const char* wifiStateName(WifiState s) {
  switch (s) {
    case WS_STA_CONNECTED:    return "STA_CONNECTED";
    case WS_STA_RECONNECTING: return "STA_RECONNECTING";
    case WS_AP_CONFIG:        return "AP_CONFIG";
    case WS_AP_DATA:          return "AP_DATA";
    case WS_AP_RETRYING:      return "AP_RETRYING";
  }
  return "?";
}

static void setWifiState(WifiState s) {
  if (s == wifiState) return;
  Logger.printf("[WiFi FSM] %s -> %s\n", wifiStateName(wifiState), wifiStateName(s));
  wifiState = s;
  stateEnteredAt = millis();
}

static inline bool isApActive() {
  return wifiState == WS_AP_CONFIG ||
         wifiState == WS_AP_DATA   ||
         wifiState == WS_AP_RETRYING;
}

// ── Smart-connect : distinguer "AP absent" vs "echec transitoire" ──
// Strategie eprouvee (cf. ESPHome, Tasmota) : on tente une fois, si echec on
// scan pour voir si l'AP est physiquement la. Si absent -> mode AP direct
// (capteur deplace, inutile de retry). Si present + auth fail -> mode AP direct
// (mdp probablement faux). Sinon -> on retry une fois (probable bug temporaire :
// box en train de rebooter, surcharge, etc.) avant de tomber en mode AP.

#define WIFI_MAX_ATTEMPTS 2  // tentatives totales avant fallback AP

enum WifiFailGroup {
  WFG_NONE,            // pas d'echec (encore)
  WFG_AP_NOT_FOUND,    // SSID introuvable
  WFG_AUTH,            // auth refusee (mdp faux)
  WFG_AP_REJECT,       // AP nous a refoule
  WFG_TRANSIENT,       // autre / inconnu / transitoire
};

// Forward declarations: onWifiEvent (defined just below) calls these helpers
// to format the disconnect reason in the log line; their full definitions
// live further down with the rest of the WiFi connection helpers.
static WifiFailGroup classifyReason(uint8_t r);
static const char* failGroupLabel(WifiFailGroup g);

static volatile uint8_t lastDisconnectReason = 0;

static void onWifiEvent(WiFiEvent_t e, WiFiEventInfo_t info) {
  // Log every significant WiFi event so the /logs page traces the full
  // life-cycle (association, disconnect, IP, AP clients). Routine /noisy
  // events (AUTHMODE_CHANGE etc.) are intentionally skipped.
  switch (e) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Logger.println("[WiFi event] STA_START");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Logger.println("[WiFi event] STA_CONNECTED (associated)");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      lastDisconnectReason = info.wifi_sta_disconnected.reason;
      Logger.printf("[WiFi event] STA_DISCONNECTED reason=%u (%s)\n",
                    lastDisconnectReason,
                    failGroupLabel(classifyReason(lastDisconnectReason)));
      if (wifiState == WS_STA_CONNECTED) {
        displaySetNetStatus(NET_OFFLINE);
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Logger.printf("[WiFi event] STA_GOT_IP %s\n", WiFi.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      Logger.println("[WiFi event] STA_LOST_IP");
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      Logger.println("[WiFi event] AP_START");
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      Logger.println("[WiFi event] AP_STOP");
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      Logger.println("[WiFi event] AP_STACONNECTED (client joined hotspot)");
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Logger.println("[WiFi event] AP_STADISCONNECTED (client left hotspot)");
      break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
      // The scan-done count is fetched separately by the scan handler; we
      // just note that a scan completed.
      Logger.println("[WiFi event] SCAN_DONE");
      break;
    default:
      break;
  }
}

// Classification basee sur les wifi_err_reason_t d'ESP-IDF.
// Sur arduino-esp32 3.x un mauvais mdp WPA2 donne typiquement 15 (4WAY_HANDSHAKE_TIMEOUT)
// ou 204 (HANDSHAKE_TIMEOUT), rarement 202 (AUTH_FAIL). On groupe tout l'auth ensemble.
static WifiFailGroup classifyReason(uint8_t r) {
  switch (r) {
    case 200:  // BEACON_TIMEOUT
    case 201:  // NO_AP_FOUND
      return WFG_AP_NOT_FOUND;
    case 0:    // legacy bug ESP-IDF #2359
    case 15:   // 4WAY_HANDSHAKE_TIMEOUT (mdp WPA2)
    case 16:   // GROUP_KEY_UPDATE_TIMEOUT
    case 23:   // IEEE_802_1X_AUTH_FAILED
    case 24:   // CIPHER_SUITE_REJECTED
    case 202:  // AUTH_FAIL
    case 204:  // HANDSHAKE_TIMEOUT
      return WFG_AUTH;
    case 5:    // ASSOC_TOOMANY
    case 6:    // NOT_AUTHED
    case 7:    // NOT_ASSOCED
    case 13:   // IE_INVALID
    case 14:   // MIC_FAILURE
    case 203:  // ASSOC_FAIL
      return WFG_AP_REJECT;
    default:
      return WFG_TRANSIENT;
  }
}

static const char* failGroupLabel(WifiFailGroup g) {
  switch (g) {
    case WFG_AP_NOT_FOUND: return "AP introuvable";
    case WFG_AUTH:         return "mdp/auth refuse";
    case WFG_AP_REJECT:    return "AP a refoule";
    case WFG_TRANSIENT:    return "transitoire/inconnu";
    default:               return "rien";
  }
}

// Tentative de connexion avec timeout. Reset le reason avant pour pouvoir
// le lire apres. Renvoie true si connecte avant le timeout.
static bool wifiTryConnect(const String& ssid, const String& password, unsigned long timeoutMs) {
  lastDisconnectReason = 0;
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(500);
    displayShowWifiDots();
    Logger.print(".");
  }
  Logger.println();
  return WiFi.status() == WL_CONNECTED;
}

// Lance un scan synchrone (~2-4s) et renvoie true si le SSID est visible.
static bool wifiSsidIsVisible(const String& ssid) {
  Logger.printf("[WiFi] Scan pour verifier presence de '%s'...\n", ssid.c_str());
  int n = WiFi.scanNetworks();
  bool found = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == ssid) {
      found = true;
      Logger.printf("[WiFi] SSID trouve au scan (RSSI %d dBm)\n", WiFi.RSSI(i));
      break;
    }
  }
  if (!found) Logger.printf("[WiFi] SSID '%s' absent du scan (%d reseaux vus)\n", ssid.c_str(), n);
  WiFi.scanDelete();
  return found;
}

// Résultats du scan WiFi (mis en cache)
static int scanCount = -1;
static bool scanInProgress = false;

// CSS commun (stocké en PROGMEM pour économiser la RAM)
static const char CSS[] PROGMEM = R"(
body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;margin:0;padding:20px;}
.header{max-width:1200px;margin:0 auto 15px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px;}
.header h1{margin:0;flex:1;}
.header .version{margin:0;flex:1;text-align:center;}
.refresh-btn{background:#16213e;border:1px solid #4fc3f7;color:#4fc3f7;padding:8px 16px;border-radius:8px;
cursor:pointer;font-size:0.9em;width:auto;transition:background 0.2s;}
.refresh-btn:hover{background:#0f3460;}
.grid{max-width:1200px;margin:0 auto;display:grid;grid-template-columns:1fr;gap:15px;}
@media(min-width:600px){.grid{grid-template-columns:repeat(2,1fr);}}
@media(min-width:960px){.grid{grid-template-columns:repeat(3,1fr);}}
.card{background:#16213e;border-radius:12px;padding:20px;box-shadow:0 4px 15px rgba(0,0,0,0.3);}
.card.wide{grid-column:1/-1;}
@media(min-width:960px){.card.span2{grid-column:span 2;}}
h1{text-align:center;color:#4fc3f7;margin-bottom:5px;}
h2{color:#4fc3f7;margin:0 0 15px 0;font-size:1.1em;}
.version{text-align:center;color:#888;font-size:0.85em;margin-bottom:20px;}
.wifi-item{display:flex;justify-content:space-between;align-items:center;padding:10px;margin:5px 0;
background:#0f3460;border-radius:8px;cursor:pointer;transition:background 0.2s;}
.wifi-item:hover{background:#1a4a7a;}
.wifi-name{font-weight:bold;}
.wifi-rssi{color:#888;font-size:0.85em;}
.lock{color:#ffa726;margin-left:8px;}
input[type=text],input[type=password]{width:100%;padding:10px;margin:8px 0;border:1px solid #333;
border-radius:8px;background:#0f3460;color:#fff;font-size:1em;box-sizing:border-box;}
.pw-wrap{position:relative;}
.pw-wrap input{padding-right:40px;}
.pw-toggle{position:absolute;right:8px;top:50%;transform:translateY(-50%);background:none;border:none;
color:#888;font-size:1.2em;cursor:pointer;width:auto;padding:4px;margin:0;}
.pw-toggle:hover{color:#fff;background:none;}
button{width:100%;padding:12px;margin:8px 0;border:none;border-radius:8px;
background:#4fc3f7;color:#1a1a2e;font-size:1em;font-weight:bold;cursor:pointer;transition:background 0.2s;}
button:hover{background:#81d4fa;}
button.danger{background:#ef5350;}
button.danger:hover{background:#f44336;}
.status{padding:10px;border-radius:8px;margin:10px 0;text-align:center;}
.status.ok{background:#1b5e20;color:#a5d6a7;}
.status.ap{background:#e65100;color:#ffcc80;}
.status.info{background:#0d3a5e;color:#90caf9;}
.status.err{background:#b71c1c;color:#ef9a9a;}
.save-status{margin-top:8px;}
.scan-info{color:#888;font-size:0.85em;text-align:center;margin:10px 0;}
.wifi-form{padding:10px 10px 5px;margin:5px 0;background:#0a2040;border-radius:8px;display:none;}
.wifi-form.active{display:block;}
.wifi-item.selected{background:#1a4a7a;border:1px solid #4fc3f7;}
.data-row{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid #0f3460;}
.data-row:last-child{border-bottom:none;}
.data-label{color:#888;}
.data-value{font-weight:bold;color:#e0e0e0;}
.data-value.good{color:#a5d6a7;}
.data-value.warn{color:#ffcc80;}
.data-value.bad{color:#ef9a9a;}
.data-unit{color:#666;font-weight:normal;font-size:0.85em;}
.sensor-badge{display:inline-block;padding:3px 8px;border-radius:4px;font-size:0.8em;margin:2px;}
.sensor-badge.ok{background:#1b5e20;color:#a5d6a7;}
.sensor-badge.err{background:#b71c1c;color:#ef9a9a;}
.sensor-badge.off{background:#37474f;color:#90a4ae;text-decoration:line-through;}
.sensor-badge.warm{background:#4e342e;color:#ffcc80;}
button.update{background:#ff9800;}
button.update:hover{background:#ffa726;}
#ota-btn{display:none;}
.toggle-row{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid #0f3460;}
.toggle-row:last-child{border-bottom:none;}
.toggle-row.locked{opacity:0.4;}
.toggle-row.locked span{color:#666;}
.toggle-hint{font-size:0.75em;color:#888;margin-left:6px;}
.switch{position:relative;width:40px;height:22px;flex-shrink:0;}
.switch input{opacity:0;width:0;height:0;}
.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#333;border-radius:22px;transition:.3s;}
.slider:before{content:"";position:absolute;height:16px;width:16px;left:3px;bottom:3px;background:#888;border-radius:50%;transition:.3s;}
input:checked+.slider{background:#4fc3f7;}
input:checked+.slider:before{transform:translateX(18px);background:#fff;}
)";

static const char JS[] PROGMEM = R"(
function togglePw(btn){
  var inp=btn.parentElement.querySelector('input');
  if(inp.type==='password'){inp.type='text';btn.textContent='&#128584;';}
  else{inp.type='password';btn.textContent='&#128065;';}
}
function selectWifi(el,ssid){
  document.querySelectorAll('.wifi-form').forEach(f=>f.classList.remove('active'));
  document.querySelectorAll('.wifi-item').forEach(i=>i.classList.remove('selected'));
  el.classList.add('selected');
  var form=el.nextElementSibling;
  form.classList.add('active');
  var pw=form.querySelector('input[type=password]');
  if(pw)pw.focus();
}
function scanWifi(){
  var b=document.getElementById('scan-btn');
  var c=document.getElementById('wifi-list');
  b.disabled=true;b.textContent=L.searching;
  fetch('/scan').then(r=>r.json()).then(function(nets){
    var h='<div class="scan-info">'+nets.length+L.networksFound+'</div>';
    nets.forEach(function(w){
      var sig=w.rssi>-50?'&#9679;&#9679;&#9679;&#9679;':w.rssi>-60?'&#9679;&#9679;&#9679;&#9675;':w.rssi>-70?'&#9679;&#9679;&#9675;&#9675;':'&#9679;&#9675;&#9675;&#9675;';
      h+="<div class='wifi-item' onclick=\"selectWifi(this,'"+w.ssid+"')\">";
      h+="<span class='wifi-name'>"+w.ssid;
      if(w.encrypted)h+="<span class='lock'>&#128274;</span>";
      h+="</span><span class='wifi-rssi'>"+sig+" "+w.rssi+"dBm</span></div>";
      h+="<div class='wifi-form'><form action='/save' method='POST'>";
      h+="<input type='hidden' name='ssid' value='"+w.ssid+"'>";
      if(w.encrypted)h+="<div class='pw-wrap'><input type='password' name='password' placeholder='"+L.password+"'><button type='button' class='pw-toggle' onclick='togglePw(this)'>&#128065;</button></div>";
      else h+="<input type='hidden' name='password' value=''>";
      h+="<button type='submit'>"+L.connect+"</button></form></div>";
    });
    if(nets.length==0)h='<div class="scan-info">'+L.noNetwork+'</div>';
    c.innerHTML=h;
    b.disabled=false;b.textContent=L.refreshNetworks;
  }).catch(function(){
    b.disabled=false;b.textContent=L.refreshNetworks;
  });
}
function checkUpdate(){
  var d=document.getElementById('ota-status');
  var b=document.getElementById('ota-btn');
  d.innerHTML="<span class='data-label'>"+L.checking+"</span>";
  fetch('/check-update').then(r=>r.json()).then(function(j){
    if(j.update){
      d.innerHTML="<span class='data-value warn'>"+L.newVersion+j.remote+"</span>";
      b.textContent=L.updateTo+j.remote;
      b.onclick=function(){doUpdate()};
      b.style.display='block';
    }else if(j.error){
      var h="<span class='data-value bad'>"+L.failed+j.error+"</span>";
      if(j.detail)h+="<br><span style='font-size:0.8em;color:#888'>"+j.detail+"</span>";
      d.innerHTML=h;
    }else{
      d.innerHTML="<span class='data-value good'>"+L.upToDate+j.current+")</span>";
    }
  }).catch(function(){
    d.innerHTML="<span class='data-value bad'>"+L.connError+"</span>";
  });
}
function doUpdate(){
  if(!confirm(L.confirmUpdate))return;
  var d=document.getElementById('ota-status');
  var b=document.getElementById('ota-btn');
  b.style.display='none';
  d.innerHTML="<span class='data-label'>"+L.downloading+"</span>";
  fetch('/do-update').then(r=>r.json()).then(function(j){
    if(j.ok){
      d.innerHTML="<span class='data-value good'>"+L.updateOk+"</span>";
    }else{
      d.innerHTML="<span class='data-value bad'>"+L.failed+j.error+"</span>";
      b.style.display='block';
    }
  }).catch(function(){
    d.innerHTML="<span class='data-value good'>"+L.updateRunning+"</span>";
  });
}
// Live dashboard updates without a full page reload. We refetch /, parse
// only the .grid container, and swap it in place. Scroll position, focus
// outside .grid, and CSS animation state are preserved. Inputs INSIDE
// .grid (CO2 thresholds, brightness) are replaced, so the periodic loop
// pauses while the user is actively interacting.
function refreshUI(){
  return fetch('/').then(function(r){return r.text();}).then(function(html){
    var doc = new DOMParser().parseFromString(html, 'text/html');
    var oldGrid = document.querySelector('.grid');
    var newGrid = doc.querySelector('.grid');
    if (!oldGrid || !newGrid) return;
    // Preserve cards marked [data-keep]: they hold accumulated state (live
    // log buffer + scroll position, OTA progress, running setIntervals, etc)
    // that a naive swap would wipe — making the logs flash "Chargement..."
    // every 30s and leaking zombie tailLogs() intervals on each refresh.
    newGrid.querySelectorAll('[data-keep]').forEach(function(stub){
      var id = stub.getAttribute('data-keep');
      var keeper = oldGrid.querySelector('[data-keep="'+id+'"]');
      if (keeper) stub.replaceWith(keeper);
    });
    oldGrid.replaceWith(newGrid);
  }).catch(function(){});
}
(function(){
  var lastTouch = 0;
  ['input','change','keydown','mousedown'].forEach(function(ev){
    addEventListener(ev, function(){ lastTouch = Date.now(); }, true);
  });
  setInterval(function(){
    var ae = document.activeElement;
    if (ae && ['INPUT','TEXTAREA','SELECT'].indexOf(ae.tagName) >= 0) return;
    if (Date.now() - lastTouch < 5000) return;
    refreshUI();
  }, 30000);
  document.addEventListener('visibilitychange', function(){
    if (!document.hidden) refreshUI();   // instant refresh on tab focus
  });
})();
)";

// =============================================
// Pages Web (envoi par chunks)
// =============================================

static void sendHeader() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  server.sendContent(i18nGetLang() == LANG_EN
    ? "<!DOCTYPE html><html lang='en'><head>"
    : "<!DOCTYPE html><html lang='fr'><head>");
  server.sendContent(
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ModuleAir</title><style>");
  server.sendContent(CSS);
  server.sendContent("</style>");
  server.sendContent(jsLangDict());   // <script>var L={...};</script> for the static JS
  server.sendContent("<script>");
  server.sendContent(JS);
  server.sendContent("</script></head><body>");
}

static void sendFooter() {
  server.sendContent("</body></html>");
  server.sendContent("");  // Fin du chunked transfer
}

static void startScanIfNeeded() {
  if (!scanInProgress && scanCount < 0) {
    Logger.println("[WiFi] Starting async scan...");
    WiFi.scanNetworks(true);  // async = true
    scanInProgress = true;
  }
}

static String formatUptime(unsigned long ms) {
  unsigned long sec = ms / 1000;
  unsigned long min = sec / 60;
  unsigned long hrs = min / 60;
  unsigned long days = hrs / 24;
  sec %= 60; min %= 60; hrs %= 24;
  char buf[32];
  if (days > 0) snprintf(buf, sizeof(buf), "%lu%s %02luh%02lu", days, TR().uptime_day, hrs, min);
  else if (hrs > 0) snprintf(buf, sizeof(buf), "%luh%02lum%02lu", hrs, min, sec);
  else snprintf(buf, sizeof(buf), "%lum%02lus", min, sec);
  return String(buf);
}


static String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) out += ' ';
        else out += c;
        break;
    }
  }
  return out;
}

static inline const char* jsonBool(bool v) {
  return v ? "true" : "false";
}

static const char* apiWifiStateName() {
  switch (wifiState) {
    case WS_STA_CONNECTED:    return "STA_MODE";
    case WS_STA_RECONNECTING: return "RECONNECTING";
    case WS_AP_CONFIG:
    case WS_AP_DATA:          return "AP_MODE";
    case WS_AP_RETRYING:      return "FALLBACK_AP";
  }
  return "DISCONNECTED";
}

static void jsonAddStringField(String& json, const char* key, const String& value, bool& first) {
  if (!first) json += ',';
  first = false;
  json += '"'; json += key; json += "\":\""; json += jsonEscape(value); json += '"';
}

static void jsonAddBoolField(String& json, const char* key, bool value, bool& first) {
  if (!first) json += ',';
  first = false;
  json += '"'; json += key; json += "\":"; json += jsonBool(value);
}

static void jsonAddIntField(String& json, const char* key, long value, bool& first) {
  if (!first) json += ',';
  first = false;
  json += '"'; json += key; json += "\":"; json += String(value);
}

static void jsonAddFloatField(String& json, const char* key, float value, uint8_t decimals, bool& first) {
  if (!first) json += ',';
  first = false;
  json += '"'; json += key; json += "\":"; json += String(value, (unsigned int)decimals);
}

static void handleRootConnected() {
  sendHeader();

  server.sendContent(
    "<div class='header'>"
    "<h1>ModuleAir</h1>"
    "<div class='version'>Firmware v" FIRMWARE_VERSION "</div>"
    "<button class='refresh-btn' onclick='location.reload()'>&#8635; " + String(TR().web_refresh) + "</button>"
    "</div>");

  const SensorData& d = sensorsGetData();
  unsigned long ago = (d.lastReadTime > 0) ? (millis() - d.lastReadTime) / 1000 : 0;

  server.sendContent("<div class='grid'>");

  // ── Banniere d'alertes (visible uniquement s'il y a des erreurs) ──
  // On ne pollue pas l'UI quand tout va bien : la banniere n'apparait
  // que si au moins un capteur active est en erreur. Et on attend qu'au
  // moins un cycle de mesure ait eu lieu, sinon au boot tous les _ok
  // sont a false (zero-init du struct) et on afficherait une fausse
  // alerte "tous les capteurs introuvables" pendant les 60 premieres
  // secondes apres reboot.
  {
    const SensorSettings& sc = settingsGetSensors();
    bool readyToAlert = d.lastReadTime > 0;
    bool hasAlerts = readyToAlert && (
        (!d.pm_ok    && sc.npm_enabled)    ||
        (!d.co2_ok   && sc.mhz19_enabled)  ||
        (!d.bme_ok   && sc.bme280_enabled) ||
        // CCS811 : alerte rouge UNIQUEMENT si vraiment absent (pas d'ACK I2C).
        // En chauffe (présent mais pas de data) → pas d'alerte, c'est normal.
        (d.ccs_state == SENSOR_ABSENT && sc.ccs811_enabled) ||
        (!d.sfa40_ok && sc.sfa40_enabled));

    if (hasAlerts) {
      String alerts = "<div class='card wide' style='background:#3e1a1a;border-left:4px solid #ef5350'>"
                      "<h2 style='color:#ef9a9a;display:flex;align-items:center;gap:10px;margin-bottom:8px'>"
                      "<span style='font-size:1.4em'>&#9888;</span> " + String(TR().alert_title) +
                      "</h2>";

      // NextPM — on decode npm_status pour donner la cause precise
      if (!d.pm_ok && sc.npm_enabled) {
        String detail;
        if (d.npmStatus == 0xFF) {
          detail = TR().npm_mute;
        } else if (d.npmStatus == 0) {
          detail = TR().npm_aberrant;
        } else {
          const char* sep = "";
          if (d.npmStatus & 0x80) { detail += sep; detail += TR().npm_laser;    sep = ", "; }
          if (d.npmStatus & 0x40) { detail += sep; detail += TR().npm_mem;      sep = ", "; }
          if (d.npmStatus & 0x20) { detail += sep; detail += TR().npm_fan;      sep = ", "; }
          if (d.npmStatus & 0x10) { detail += sep; detail += TR().npm_th;       sep = ", "; }
          if (d.npmStatus & 0x08) { detail += sep; detail += TR().npm_humid;    sep = ", "; }
          if (d.npmStatus & 0x04) { detail += sep; detail += TR().npm_notready; sep = ", "; }
          if (d.npmStatus & 0x02) { detail += sep; detail += TR().npm_degraded; sep = ", "; }
          if (d.npmStatus & 0x01) { detail += sep; detail += TR().npm_sleep;    sep = ", "; }
          if (detail.length() == 0) detail = "status 0x" + String(d.npmStatus, HEX);
        }
        alerts += "<div class='data-row'><span class='data-label'>NextPM (PM)</span>"
                  "<span class='data-value bad' style='text-align:right'>" + detail + "</span></div>";
      }

      if (!d.co2_ok && sc.mhz19_enabled) {
        alerts += "<div class='data-row'><span class='data-label'>MH-Z19 (CO2)</span>"
                  "<span class='data-value bad' style='text-align:right'>" + String(TR().co2_read_err) + "</span></div>";
      }
      if (!d.bme_ok && sc.bme280_enabled) {
        alerts += "<div class='data-row'><span class='data-label'>BME280 (T/H/P)</span>"
                  "<span class='data-value bad' style='text-align:right'>" + String(TR().sensor_not_found) + "</span></div>";
      }
      if (d.ccs_state == SENSOR_ABSENT && sc.ccs811_enabled) {
        alerts += "<div class='data-row'><span class='data-label'>" + String(TR().sensor_cov_name) + "</span>"
                  "<span class='data-value bad' style='text-align:right'>" + String(TR().sensor_not_found) + "</span></div>";
      }
      if (!d.sfa40_ok && sc.sfa40_enabled) {
        alerts += "<div class='data-row'><span class='data-label'>SFA40 (HCHO)</span>"
                  "<span class='data-value bad' style='text-align:right'>" + String(TR().sensor_not_found) + "</span></div>";
      }

      alerts += "</div>";
      server.sendContent(alerts);
    }
  }

  // Status WiFi
  String chunk = "<div class='card span2'><h2>" + String(TR().card_connection) + "</h2>";
  chunk += "<div class='data-row'><span class='data-label'>" + String(TR().web_network) + "</span><span class='data-value'>" + WiFi.SSID() + "</span></div>";
  chunk += "<div class='data-row'><span class='data-label'>IP</span><span class='data-value'>" + WiFi.localIP().toString() + "</span></div>";
  {
    int rssi = WiFi.RSSI();
    int bars = rssi > -50 ? 4 : rssi > -60 ? 3 : rssi > -70 ? 2 : 1;
    const char* label = bars == 4 ? TR().sig_excellent : bars == 3 ? TR().sig_good : bars == 2 ? TR().sig_medium : TR().sig_weak;
    const char* color = bars >= 3 ? "#a5d6a7" : bars == 2 ? "#ffcc80" : "#ef9a9a";
    String svg = "<svg width='24' height='18' viewBox='0 0 24 18' style='vertical-align:middle;margin-right:6px;'>";
    for (int i = 0; i < 4; i++) {
      int h = 5 + i * 4;
      int y = 18 - h;
      String fill = (i < bars) ? String(color) : "#333";
      svg += "<rect x='" + String(i * 6) + "' y='" + String(y) + "' width='5' height='" + String(h) + "' rx='1' fill='" + fill + "'/>";
    }
    svg += "</svg>";
    chunk += "<div class='data-row'><span class='data-label'>" + String(TR().web_signal) + "</span><span class='data-value' style='color:" + String(color) + "'>" + svg + label + " <span class='data-unit'>(" + String(rssi) + " dBm)</span></span></div>";
  }
  chunk += "<div class='data-row'><span class='data-label'>" + String(TR().web_uptime) + "</span><span class='data-value'>" + formatUptime(millis()) + "</span></div>";
  chunk += "</div>";
  server.sendContent(chunk);

  // Particules fines
  chunk = "<div class='card'><h2>" + String(TR().card_pm) + "</h2>";
  if (d.pm_ok) {
    chunk += "<div class='data-row'><span class='data-label'>PM1.0</span><span class='data-value'>" + String(d.pm1, 1) + " <span class='data-unit'>ug/m3</span></span></div>";
    chunk += "<div class='data-row'><span class='data-label'>PM2.5</span><span class='data-value " + String(d.pm25 < 15 ? "good" : d.pm25 < 35 ? "warn" : "bad") + "'>" + String(d.pm25, 1) + " <span class='data-unit'>ug/m3</span></span></div>";
    chunk += "<div class='data-row'><span class='data-label'>PM10</span><span class='data-value " + String(d.pm10 < 45 ? "good" : d.pm10 < 80 ? "warn" : "bad") + "'>" + String(d.pm10, 1) + " <span class='data-unit'>ug/m3</span></span></div>";
  } else {
    chunk += "<div class='scan-info'>" + String(TR().web_waiting) + "</div>";
  }
  chunk += "</div>";
  server.sendContent(chunk);

  // CO2
  chunk = "<div class='card'><h2>" + String(TR().card_co2) + "</h2>";
  if (d.co2_ok) {
    String co2class = d.co2 < 800 ? "good" : d.co2 < 1200 ? "warn" : "bad";
    chunk += "<div class='data-row'><span class='data-label'>CO2</span><span class='data-value " + co2class + "'>" + String(d.co2) + " <span class='data-unit'>ppm</span></span></div>";
  } else {
    chunk += "<div class='scan-info'>" + String(TR().web_waiting) + "</div>";
  }
  chunk += "</div>";
  server.sendContent(chunk);

  // COV (CCS811) : la valeur dès qu'on en a une, sinon un simple "en attente"
  // (neutre) tant que la 1ère mesure n'est pas tombée. Le rouge "non détecté"
  // est géré ailleurs (bannière d'alerte) et UNIQUEMENT si l'I2C ne répond pas.
  chunk = "<div class='card'><h2>" + String(TR().card_cov) + "</h2>";
  if (d.ccs_ok) {
    String tvocClass = d.tvoc < 220 ? "good" : d.tvoc < 660 ? "warn" : "bad";
    String eco2Class = d.eco2 < 800 ? "good" : d.eco2 < 1200 ? "warn" : "bad";
    chunk += "<div class='data-row'><span class='data-label'>TVOC</span><span class='data-value " + tvocClass + "'>" + String(d.tvoc) + " <span class='data-unit'>ppb</span></span></div>";
    chunk += "<div class='data-row'><span class='data-label'>eCO2</span><span class='data-value " + eco2Class + "'>" + String(d.eco2) + " <span class='data-unit'>ppm</span></span></div>";
  } else {
    chunk += "<div class='scan-info'>" + String(TR().web_waiting) + "</div>";
  }
  chunk += "</div>";
  server.sendContent(chunk);

  // Formaldéhyde (SFA40)
  chunk = "<div class='card'><h2>" + String(TR().card_hcho) + "</h2>";
  if (d.sfa40_ok) {
    String hchoClass = d.hcho < 30 ? "good" : d.hcho < 100 ? "warn" : "bad";
    chunk += "<div class='data-row'><span class='data-label'>HCHO</span><span class='data-value " + hchoClass + "'>" + String(d.hcho, 1) + " <span class='data-unit'>ppb</span></span></div>";
  } else {
    chunk += "<div class='scan-info'>" + String(TR().web_waiting) + "</div>";
  }
  chunk += "</div>";
  server.sendContent(chunk);

  // Température / Humidité / Pression
  chunk = "<div class='card'><h2>" + String(TR().card_env) + "</h2>";
  if (d.bme_ok) {
    chunk += "<div class='data-row'><span class='data-label'>" + String(TR().web_temperature) + "</span><span class='data-value'>" + String(d.temperature, 1) + " <span class='data-unit'>°C</span></span></div>";
    chunk += "<div class='data-row'><span class='data-label'>" + String(TR().web_humidity) + "</span><span class='data-value'>" + String(d.humidity, 1) + " <span class='data-unit'>%</span></span></div>";
    chunk += "<div class='data-row'><span class='data-label'>" + String(TR().web_pressure) + "</span><span class='data-value'>" + String(d.pressure, 1) + " <span class='data-unit'>hPa</span></span></div>";
  } else {
    chunk += "<div class='scan-info'>" + String(TR().web_waiting) + "</div>";
  }
  chunk += "</div>";
  server.sendContent(chunk);

  // Infos système
  chunk = "<div class='card'><h2>" + String(TR().card_system) + "</h2>";
  chunk += "<div class='data-row'><span class='data-label'>Device ID</span><span class='data-value'>" + deviceId + "</span></div>";
  chunk += "<div class='data-row'><span class='data-label'>Firmware</span><span class='data-value'>v" FIRMWARE_VERSION "</span></div>";
  chunk += "<div class='data-row'><span class='data-label'>ESP32</span><span class='data-value'>" + String(ESP.getChipModel()) + " rev" + String(ESP.getChipRevision()) + "</span></div>";
  chunk += "<div class='data-row'><span class='data-label'>CPU</span><span class='data-value'>" + String(ESP.getCpuFreqMHz()) + " <span class='data-unit'>MHz</span></span></div>";
  chunk += "<div class='data-row'><span class='data-label'>" + String(TR().web_free_ram) + "</span><span class='data-value'>" + String(ESP.getFreeHeap() / 1024) + " <span class='data-unit'>KB</span></span></div>";
  chunk += "<div class='data-row'><span class='data-label'>MAC</span><span class='data-value'>" + WiFi.macAddress() + "</span></div>";
  // Quad-state badge: ok / err / off / warm.
  //  - off  : capteur desactive par l'utilisateur (gris barre)
  //  - warm : pas encore lu une seule fois depuis le boot (orange "...")
  //           sinon on afficherait "err" en rouge pendant les ~60 premieres
  //           secondes apres reboot a cause du zero-init du struct SensorData
  //  - ok   : capteur actif et lecture OK (vert)
  //  - err  : capteur actif mais lecture KO (rouge)
  {
    const SensorSettings& sc = settingsGetSensors();
    bool warmingUp = (d.lastReadTime == 0);
    auto badgeClass = [&](bool enabled, bool ok) {
      if (!enabled) return "off";
      if (warmingUp) return "warm";
      return ok ? "ok" : "err";
    };
    // CCS811 : badge tri-état (présent-en-chauffe = orange "warm", pas rouge).
    auto ccsBadgeClass = [&]() -> const char* {
      if (!sc.ccs811_enabled) return "off";
      if (warmingUp) return "warm";
      if (d.ccs_state == SENSOR_ABSENT)  return "err";
      if (d.ccs_state == SENSOR_WARMING) return "warm";
      return "ok";
    };
    chunk += "<div class='data-row'><span class='data-label'>" + String(TR().web_sensors) + "</span><span class='data-value'>";
    chunk += "<span class='sensor-badge " + String(badgeClass(sc.npm_enabled,    d.pm_ok))    + "'>NextPM</span>";
    chunk += "<span class='sensor-badge " + String(badgeClass(sc.mhz19_enabled,  d.co2_ok))   + "'>MH-Z19</span>";
    chunk += "<span class='sensor-badge " + String(badgeClass(sc.bme280_enabled, d.bme_ok))   + "'>BME280</span>";
    chunk += "<span class='sensor-badge " + String(ccsBadgeClass())   + "'>CCS811</span>";
    chunk += "<span class='sensor-badge " + String(badgeClass(sc.sfa40_enabled,  d.sfa40_ok)) + "'>SFA40</span>";
    chunk += "</span></div>";
  }
  if (d.lastReadTime > 0) {
    chunk += "<div class='data-row'><span class='data-label'>" + String(TR().web_last_measure) + "</span><span class='data-value'>" + String(TR().web_ago_pre) + String(ago) + String(TR().web_ago_post) + "</span></div>";
  }
  chunk += "<div class='data-row'><span class='data-label'>" + String(TR().web_brightness) + "</span><span class='data-value'>"
           "<span id='bri-val'>" + String(displayGetBrightness()) + "</span>/255"
           "<input type='range' min='0' max='255' value='" + String(displayGetBrightness()) + "' style='width:80px;margin-left:8px;vertical-align:middle;'"
           " oninput=\"document.getElementById('bri-val').textContent=this.value;document.getElementById('bri-warn').style.display=this.value==='0'?'block':'none'\""
           " onchange=\"fetch('/set-brightness',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'val='+this.value})\">"
           "</span></div>"
           "<div id='bri-warn' style='display:" + String(displayGetBrightness() == 0 ? "block" : "none") + ";color:#ffcc80;font-size:0.8em;padding:4px 0 8px;'>"
           + String(TR().web_screen_off) + "</div>";
  chunk += "<div class='data-row'><span class='data-label'>" + String(TR().web_debug_splash) + "</span><span class='data-value'>"
           "<label style='cursor:pointer'><input type='checkbox' id='dbg-splash' "
           + String(displayGetDebugSplash() ? "checked" : "") +
           " onchange=\"fetch('/debug-splash',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'enabled='+(this.checked?'1':'0')})\"> "
           + String(TR().web_active) + "</label></span></div>";
  chunk += "</div>";
  server.sendContent(chunk);

  // Capteurs actifs. The SENSOR_*_DEFAULT macros only set the first-boot
  // state; at runtime every toggle is fully functional, including
  // re-enabling a sensor that started disabled in code.
  {
    const SensorSettings& sc = settingsGetSensors();
    chunk = "<div class='card'><h2>" + String(TR().card_active_sensors) + "</h2>"
            "<p style='color:#888;font-size:0.8em;margin:0 0 8px'>" + String(TR().web_immediate) + "</p>";
    const char* sensorNames[] = { "NextPM (PM)", "MH-Z19 (CO2)", "BME280 (T/H/P)", TR().sensor_cov_name, "SFA40 (HCHO)" };
    const char* sensorKeys[]  = { "npm", "mhz19", "bme280", "ccs811", "sfa40" };
    bool sensorVals[]         = { sc.npm_enabled, sc.mhz19_enabled, sc.bme280_enabled, sc.ccs811_enabled, sc.sfa40_enabled };
    for (int i = 0; i < 5; i++) {
      chunk += "<div class='toggle-row'><span>" + String(sensorNames[i]) + "</span>";
      chunk += "<label class='switch'><input type='checkbox'";
      if (sensorVals[i]) chunk += " checked";
      chunk += " onchange=\"fetch('/set-sensor',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key=" + String(sensorKeys[i]) + "&val='+(this.checked?'1':'0')}).then(refreshUI)\">";
      chunk += "<span class='slider'></span></label></div>";
    }
    chunk += "</div>";
    server.sendContent(chunk);
  }

  // NB : aucune carte "Capteur CO2" ici. MH-Z19 et SenseAir S8/S88 partagent le
  // meme connecteur et sont reconnus automatiquement au runtime — rien a regler,
  // donc rien a afficher. Le toggle CO2 de "Capteurs actifs" ci-dessus coupe la
  // voie entiere, quel que soit le modele branche.

  // Ecrans affichés
  {
    const ScreenSettings& ss = settingsGetScreens();
    const SensorSettings& sc = settingsGetSensors();
    chunk = "<div class='card'><h2>" + String(TR().card_matrix_screens) + "</h2>";

    // Polluants. Each pollutant screen is locked when its parent sensor is
    // disabled — we keep the user's saved preference (it'll come back when
    // they re-enable the sensor) but the toggle is greyed out and not
    // clickable to avoid the "screen toggle is on but nothing shows" trap.
    chunk += "<p style='color:#4fc3f7;font-size:0.85em;margin:0 0 6px;font-weight:bold'>" + String(TR().web_pollutants) + "</p>";
    const char* pollNames[]  = { "PM1", "PM2.5", "PM10", "CO2", "Temperature", TR().poll_humi_name, TR().poll_cov_name, "Formaldehyde" };
    const char* pollKeys[]   = { "pm1", "pm25", "pm10", "co2", "temp", "humi", "tvoc", "hcho" };
    bool pollVals[]          = { ss.pm1, ss.pm25, ss.pm10, ss.co2, ss.temp, ss.humi, ss.tvoc, ss.hcho };
    bool pollParentOn[]      = { sc.npm_enabled, sc.npm_enabled, sc.npm_enabled,
                                 sc.mhz19_enabled,
                                 sc.bme280_enabled, sc.bme280_enabled,
                                 sc.ccs811_enabled,
                                 sc.sfa40_enabled };
    for (int i = 0; i < 8; i++) {
      bool locked = !pollParentOn[i];
      chunk += "<div class='toggle-row";
      if (locked) chunk += " locked";
      chunk += "'><span>" + String(pollNames[i]);
      if (locked) chunk += "<span class='toggle-hint'>" + String(TR().web_sensor_off) + "</span>";
      chunk += "</span>";
      chunk += "<label class='switch'><input type='checkbox'";
      if (pollVals[i]) chunk += " checked";
      if (locked) chunk += " disabled";
      chunk += " onchange=\"fetch('/set-screen',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key=" + String(pollKeys[i]) + "&val='+(this.checked?'1':'0')})\">";
      chunk += "<span class='slider'></span></label></div>";
    }

    // Logos
    chunk += "<p style='color:#4fc3f7;font-size:0.85em;margin:12px 0 6px;font-weight:bold'>Logos</p>";
    const char* logoNames[] = { "ModuleAir", "AirCarto", "AtmoSud"
#ifdef BUILD_LAIRETMOI
      , "L'Air et Moi"
#endif
    };
    const char* logoKeys[] = { "logo_ma", "logo_ac", "logo_as"
#ifdef BUILD_LAIRETMOI
      , "logo_lam"
#endif
    };
    bool logoVals[] = { ss.logo_moduleair, ss.logo_aircarto, ss.logo_atmosud
#ifdef BUILD_LAIRETMOI
      , ss.logo_lairetmoi
#endif
    };
    const int logoN = sizeof(logoKeys) / sizeof(logoKeys[0]);
    for (int i = 0; i < logoN; i++) {
      chunk += "<div class='toggle-row'><span>" + String(logoNames[i]) + "</span>";
      chunk += "<label class='switch'><input type='checkbox'";
      if (logoVals[i]) chunk += " checked";
      chunk += " onchange=\"fetch('/set-screen',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key=" + String(logoKeys[i]) + "&val='+(this.checked?'1':'0')})\">";
      chunk += "<span class='slider'></span></label></div>";
    }

    chunk += "</div>";
    server.sendContent(chunk);
  }

  // Seuils
  {
    const ThresholdsCO2& co2th = settingsGetThresholdsCO2();
    chunk = "<div class='card'><h2>" + String(TR().card_thresholds) + "</h2>";
    chunk += "<p style='color:#4fc3f7;font-size:0.85em;margin:0 0 6px;font-weight:bold'>CO2 (ppm)</p>";
    chunk += "<div class='data-row'><span class='data-label' style='color:#a5d6a7'>" + String(TR().web_th_good) + "</span>";
    chunk += "<span class='data-value'><input type='number' id='co2-good' value='" + String(co2th.good) + "' style='width:60px;background:#0f3460;color:#fff;border:1px solid #333;border-radius:4px;padding:4px;text-align:center;'></span></div>";
    chunk += "<div class='data-row'><span class='data-label' style='color:#ef9a9a'>" + String(TR().web_th_bad) + "</span>";
    chunk += "<span class='data-value'><input type='number' id='co2-bad' value='" + String(co2th.bad) + "' style='width:60px;background:#0f3460;color:#fff;border:1px solid #333;border-radius:4px;padding:4px;text-align:center;'></span></div>";
    chunk += "<div style='color:#888;font-size:0.8em;padding:4px 0;'>" + String(TR().web_th_between) + "</div>";
    chunk += "<div style='display:flex;gap:8px;margin-top:4px;'>"
             "<button style='width:auto;padding:8px 20px;' onclick=\"fetch('/set-thresholds-co2',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'good='+document.getElementById('co2-good').value+'&bad='+document.getElementById('co2-bad').value}).then(()=>{this.textContent='" + String(TR().web_ok_excl) + "';setTimeout(()=>this.textContent='" + String(TR().web_apply) + "',1500)})\">" + String(TR().web_apply) + "</button>"
             "<button style='width:auto;padding:8px 12px;background:#333;color:#aaa;' onclick=\"document.getElementById('co2-good').value='800';document.getElementById('co2-bad').value='1500';fetch('/set-thresholds-co2',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'good=800&bad=1500'}).then(()=>{this.textContent='" + String(TR().web_restored) + "';setTimeout(()=>this.textContent='" + String(TR().web_default) + "',1500)})\">" + String(TR().web_default) + "</button>"
             "</div>";
    chunk += "</div>";
    server.sendContent(chunk);
  }

  // Envoi des donnees — card purement DECLARATIVE (port NebuleAir) : montre ou
  // partent les mesures. AirCarto toujours ; AtmoSud selon la decision firmware
  // par capteur (stamp NVS pose a l'usine), jamais modifiable ici.
  // La ligne AtmoSud n'existe que si le binaire embarque l'URL (secrets.ini).
  {
    chunk = "<div class='card wide'><h2>" + String(TR().card_dataservers) + "</h2>";
    chunk += "<div class='data-row'><span class='data-label'>AirCarto (data.moduleair.fr)</span>"
             "<span class='data-value good'>" + String(TR().ds_always) + "</span></div>";
#ifdef ATMOSUD_SERVER_URL
    if (dataSenderIsAtmosudDevice()) {
      chunk += "<div class='data-row'><span class='data-label'>AtmoSud (MicroSpot)</span>"
               "<span class='data-value good'>" + String(TR().ds_active) + "</span></div>";
    } else {
      chunk += "<div class='data-row'><span class='data-label'>AtmoSud (MicroSpot)</span>"
               "<span class='data-value'>" + String(TR().ds_inactive) + "</span></div>";
    }
#endif
    chunk += "<div style='color:#888;font-size:0.8em;padding:4px 0;'>" + String(TR().ds_note) + "</div>";
    chunk += "</div>";
    server.sendContent(chunk);
  }

  // Logs — tail incremental avec curseur seq, smart scroll, pause/clear.
  // data-keep="logs" tells refreshUI() to leave this card alone during a
  // .grid swap so the log buffer, scroll position, pause state and the
  // tailLogs() setInterval survive across dashboard refreshes.
  server.sendContent(
    "<div class='card wide' data-keep='logs'><h2 style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px'>"
    "<span>");
  server.sendContent(TR().card_logs);
  server.sendContent(
    "</span>"
    "<span style='font-size:0.75em;font-weight:normal;color:#888'>"
    "<span id='log-status' style='margin-right:10px'>&#9679; live</span>"
    "<button id='log-pause' style='width:auto;padding:4px 10px;font-size:0.9em' onclick='toggleLogPause()'>Pause</button> "
    "<button style='width:auto;padding:4px 10px;font-size:0.9em;background:#333;color:#aaa' onclick='clearLogs()'>");
  server.sendContent(TR().web_clear);
  server.sendContent(
    "</button>"
    "</span></h2>"
    "<pre id='log-box' style='background:#0a0a1a;padding:10px;border-radius:8px;"
    "height:50vh;min-height:300px;overflow-y:auto;font-size:0.8em;color:#aaa;"
    "white-space:pre-wrap;word-break:break-all;margin:0'>");
  server.sendContent(TR().web_loading);
  server.sendContent(
    "</pre>"
    "<script>"
    "var logSeq=0,logPaused=false,logFirst=true;"
    "var logBox=document.getElementById('log-box');"
    "var logStatus=document.getElementById('log-status');"
    "function clearLogs(){logBox.textContent='';}"
    "function toggleLogPause(){"
    "logPaused=!logPaused;"
    "document.getElementById('log-pause').textContent=logPaused?'");
  server.sendContent(TR().web_resume);
  server.sendContent(
    "':'Pause';"
    "logStatus.innerHTML=logPaused?'&#9679; pause':'&#9679; live';"
    "logStatus.style.color=logPaused?'#ffcc80':'#a5d6a7';"
    "}"
    "function tailLogs(){"
    "if(logPaused)return;"
    "fetch('/logs?since='+logSeq).then(function(r){"
    "var s=parseInt(r.headers.get('X-Log-Seq'));"
    "if(!isNaN(s))logSeq=s;"
    "return r.text();"
    "}).then(function(t){"
    "if(logFirst){logBox.textContent=t||'");
  server.sendContent(TR().web_no_logs);
  server.sendContent(
    "';logFirst=false;logBox.scrollTop=logBox.scrollHeight;return;}"
    "if(!t)return;"
    "var atBottom=logBox.scrollHeight-logBox.scrollTop-logBox.clientHeight<60;"
    "logBox.textContent+=t;"
    "if(atBottom)logBox.scrollTop=logBox.scrollHeight;"
    "}).catch(function(){});"
    "}"
    "logStatus.style.color='#a5d6a7';"
    "tailLogs();setInterval(tailLogs,1000);"
    "</script></div>");

  // Mise à jour OTA — data-keep prevents the card from being wiped during
  // a refresh while an OTA check or update is in progress (otherwise the
  // user would see the status reset to empty while their click is in flight).
  server.sendContent(
    "<div class='card wide' data-keep='ota'><h2>");
  server.sendContent(TR().card_update);
  server.sendContent(
    "</h2>"
    "<div id='ota-status'></div>"
    "<button class='update' onclick='checkUpdate()'>");
  server.sendContent(TR().web_check_update);
  server.sendContent(
    "</button>"
    "<button id='ota-btn' class='update'></button>"
    "</div>");

  // Langue / Language — switches at runtime, persists in NVS (survives OTA).
  {
    String langCard = "<div class='card'><h2>" + String(TR().card_language) + "</h2>";
    langCard += "<select onchange=\"fetch('/set-lang',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'lang='+this.value}).then(()=>location.reload())\" style='width:100%;padding:10px;border-radius:8px;background:#0f3460;color:#fff;border:1px solid #333;font-size:1em;box-sizing:border-box;'>";
    langCard += String("<option value='FR'") + (i18nGetLang() == LANG_FR ? " selected" : "") + ">Francais (FR)</option>";
    langCard += String("<option value='EN'") + (i18nGetLang() == LANG_EN ? " selected" : "") + ">English (EN)</option>";
    langCard += "</select></div>";
    server.sendContent(langCard);
  }

  // Boutons oublier WiFi + redémarrer
  server.sendContent(
    "<div class='card wide'>"
    "<form action='/reset' method='POST'>"
    "<button type='submit' class='danger'>");
  server.sendContent(TR().web_forget_wifi);
  server.sendContent(
    "</button>"
    "</form>"
    "<form action='/reboot' method='POST'>"
    "<button type='submit' class='danger'>");
  server.sendContent(TR().web_reboot);
  server.sendContent(
    "</button>"
    "</form></div>");

  server.sendContent("</div>");  // Fin grid
  sendFooter();
}

static void handleRootAP() {
  // Attendre le scan
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    unsigned long start = millis();
    while (n == WIFI_SCAN_RUNNING && millis() - start < 5000) {
      delay(100);
      n = WiFi.scanComplete();
    }
  }
  if (n == WIFI_SCAN_FAILED) n = 0;

  sendHeader();

  server.sendContent(
    "<h1>ModuleAir</h1>"
    "<div class='version'>Firmware v" FIRMWARE_VERSION "</div>");

  // Status AP
  String apStatus = "<div class='card'><h2>" + String(TR().ap_status) + "</h2>"
    "<div class='status ap'>" + String(TR().ap_mode) + "<br>"
    "SSID: " + apSSID + "</div></div>";
  server.sendContent(apStatus);

  // Réseaux WiFi
  String chunk = "<div class='card'><h2>" + String(TR().ap_networks) + "</h2>";
  chunk += "<div id='wifi-list'>";
  if (n <= 0) {
    chunk += "<div class='scan-info'>" + String(TR().ap_no_network) + "</div>";
  } else {
    chunk += "<div class='scan-info'>" + String(n) + String(TR().ap_networks_found_suffix) + "</div>";
  }
  server.sendContent(chunk);

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    bool encrypted = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

    const char* signal;
    if (rssi > -50) signal = "&#9679;&#9679;&#9679;&#9679;";
    else if (rssi > -60) signal = "&#9679;&#9679;&#9679;&#9675;";
    else if (rssi > -70) signal = "&#9679;&#9679;&#9675;&#9675;";
    else signal = "&#9679;&#9675;&#9675;&#9675;";

    chunk = "<div class='wifi-item' onclick=\"selectWifi(this,'" + ssid + "')\">";
    chunk += "<span class='wifi-name'>" + ssid;
    if (encrypted) chunk += "<span class='lock'>&#128274;</span>";
    chunk += "</span>";
    chunk += "<span class='wifi-rssi'>" + String(signal) + " " + String(rssi) + "dBm</span>";
    chunk += "</div>";

    chunk += "<div class='wifi-form'>";
    chunk += "<form action='/save' method='POST'>";
    chunk += "<input type='hidden' name='ssid' value='" + ssid + "'>";
    if (encrypted) {
      chunk += "<div class='pw-wrap'><input type='password' name='password' placeholder='" + String(TR().ap_password) + "'><button type='button' class='pw-toggle' onclick='togglePw(this)'>&#128065;</button></div>";
    } else {
      chunk += "<input type='hidden' name='password' value=''>";
    }
    chunk += "<button type='submit'>" + String(TR().ap_connect) + "</button>";
    chunk += "</form></div>";

    server.sendContent(chunk);
  }
  server.sendContent("</div>");  // fin wifi-list
  server.sendContent("<button id='scan-btn' onclick='scanWifi()'>" + String(TR().ap_refresh) + "</button>");
  server.sendContent("</div>");  // fin card

  // Langue / Language — selectable from the captive portal too (handy for an
  // export device that boots into AP config before any WiFi is set).
  {
    String langCard = "<div class='card'><h2>" + String(TR().card_language) + "</h2>";
    langCard += "<select onchange=\"fetch('/set-lang',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'lang='+this.value}).then(()=>location.reload())\" style='width:100%;padding:10px;border-radius:8px;background:#0f3460;color:#fff;border:1px solid #333;font-size:1em;box-sizing:border-box;'>";
    langCard += String("<option value='FR'") + (i18nGetLang() == LANG_FR ? " selected" : "") + ">Francais (FR)</option>";
    langCard += String("<option value='EN'") + (i18nGetLang() == LANG_EN ? " selected" : "") + ">English (EN)</option>";
    langCard += "</select></div>";
    server.sendContent(langCard);
  }

  sendFooter();

  // Relancer un scan pour la prochaine visite
  WiFi.scanDelete();
  scanCount = -1;
  scanInProgress = false;
  startScanIfNeeded();
}

static void handleScan() {
  Logger.println("[Web] /scan - launching WiFi scan");

  // Scan synchrone (bloquant mais rapide, ~2s)
  int n = WiFi.scanNetworks();
  if (n < 0) n = 0;

  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i));
    json += ",\"encrypted\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";

  WiFi.scanDelete();
  scanCount = -1;
  scanInProgress = false;

  server.send(200, "application/json", json);
}

// SPA ModuleAir (web/app.html, gzippée en PROGMEM au build par web_embed.py).
// Servie en streaming send_P : rien n'est copié en heap.
static void handleApp() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", (PGM_P)WEB_INDEX_GZ, WEB_INDEX_GZ_LEN);
}

static void handleRoot() {
  Logger.printf("[Web] Client %s requested /\n", server.client().remoteIP().toString().c_str());
  if (wifiState == WS_STA_CONNECTED) {
    // Nouvelle interface (SPA). L'ancien dashboard reste accessible sur /config.
    handleApp();
  } else {
    handleRootAP();
  }
}

static void handleSave() {
  Logger.printf("[Web] Client %s requested /save\n", server.client().remoteIP().toString().c_str());
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  if (ssid.length() == 0) {
    Logger.println("[Web] Save rejected: empty SSID");
    sendHeader();
    server.sendContent(
      "<div class='card'><div class='status ap'>" + String(TR().ap_ssid_required) + "</div>"
      "<br><a href='/'><button>" + String(TR().web_back) + "</button></a></div>");
    sendFooter();
    return;
  }

  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();

  Logger.printf("[WiFi] Credentials saved - SSID: %s, password length: %d\n", ssid.c_str(), password.length());
  Logger.println("[WiFi] Restarting in 3 seconds...");

  sendHeader();
  server.sendContent(
    "<div class='card'><div class='status ok'>" + String(TR().ap_saved_title) + "<br><br>"
    "SSID: <strong>" + ssid + "</strong><br><br>"
    + String(TR().ap_saved_body) + "</div></div>");
  sendFooter();

  delay(3000);
  ESP.restart();
}

static void handleReset() {
  Logger.printf("[Web] Client %s requested /reset\n", server.client().remoteIP().toString().c_str());
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();

  Logger.println("[WiFi] Credentials cleared from NVS");
  Logger.println("[WiFi] Restarting in AP mode in 3 seconds...");

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<style>body{font-family:system-ui;background:#1a1a2e;color:#e0e0e0;"
    "display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
    ".msg{text-align:center;}</style></head><body>"
    "<div class='msg'><h2>" + String(TR().ap_forgotten) + "</h2>"
    "<p>" + String(TR().ap_restart_ap) + "</p>"
    "<p>" + String(TR().ap_connect_to) + "<strong>" + apSSID + "</strong></p></div>"
    "</body></html>");
  server.sendContent("");

  delay(1000);
  ESP.restart();
}

static void handleReboot() {
  Logger.printf("[Web] Client %s requested /reboot\n", server.client().remoteIP().toString().c_str());
  Logger.println("[System] Restarting in 3 seconds...");

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta http-equiv='refresh' content='10;url=/'>"
    "<style>body{font-family:system-ui;background:#1a1a2e;color:#e0e0e0;"
    "display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
    ".msg{text-align:center;}</style></head><body>"
    "<div class='msg'><h2>" + String(TR().reboot_title) + "</h2>"
    "<p>" + String(TR().reboot_body) + "</p></div>"
    "</body></html>");
  server.sendContent("");

  delay(1000);
  ESP.restart();
}

// =============================================
// Logs
static void handleSetSensor() {
  String key = server.arg("key");
  bool val = server.arg("val") == "1";
  settingsSetSensorEnabled(key.c_str(), val);
  Logger.printf("[Web] Sensor %s = %s\n", key.c_str(), val ? "on" : "off");
  server.send(200, "text/plain", "ok");
}

static void handleSetScreen() {
  String key = server.arg("key");
  bool val = server.arg("val") == "1";
  // Clés de toggle valides (≠ jetons d'ordre : ici les logos sont individuels).
  static const char* const SCREEN_KEYS[] = {
    "pm1", "pm25", "pm10", "co2", "temp", "humi", "tvoc", "hcho",
    "clock", "weather", "crypto",
    "logo_ma", "logo_ac", "logo_as",
#ifdef BUILD_LAIRETMOI
    "logo_lam",
#endif
  };
  bool known = false;
  for (auto k : SCREEN_KEYS) if (key == k) { known = true; break; }
  if (!known) { server.send(400, "text/plain", "invalid key"); return; }
  settingsSetScreenEnabled(key.c_str(), val);
  Logger.printf("[Web] Screen %s = %s\n", key.c_str(), val ? "on" : "off");
  server.send(200, "text/plain", "ok");
}

// Ordre de rotation des écrans (drag & drop de la SPA) — CSV de jetons,
// normalisé/validé par settingsSetScreenOrder (jetons inconnus retirés,
// manquants ré-ajoutés en fin).
static void handleSetScreenOrder() {
  String order = server.arg("order");
  if (order.isEmpty() || order.length() > 160) {
    server.send(400, "text/plain", "invalid order");
    return;
  }
  settingsSetScreenOrder(order);
  server.send(200, "text/plain", "ok");
}

static void handleSetRotation() {
  int val = server.arg("val").toInt();
  if (val < 3 || val > 60) {
    server.send(400, "text/plain", "invalid: 3..60");
    return;
  }
  settingsSetRotationSec((uint16_t)val);
  server.send(200, "text/plain", "ok");
}

// Ville météo (géocodée côté navigateur via geocoding-api.open-meteo.com).
static void handleSetWeather() {
  String city = server.arg("city");
  float lat = server.arg("lat").toFloat();
  float lon = server.arg("lon").toFloat();
  city.trim();
  if (city.isEmpty() || city.length() > 40 ||
      lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) {
    server.send(400, "text/plain", "invalid place");
    return;
  }
  pluginsSetWeatherPlace(city, lat, lon);
  server.send(200, "text/plain", "ok");
}

// Ids CoinGecko (csv). On ne garde que [a-z0-9-] et les virgules, 4 ids max —
// l'URL du fetch est construite avec cette valeur, donc on la contraint fort.
static void handleSetCrypto() {
  String ids = server.arg("ids");
  String clean;
  int nIds = 0;
  bool tokenOpen = false;
  for (size_t i = 0; i < ids.length() && clean.length() < 120; i++) {
    char ch = ids[i];
    if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-') {
      if (!tokenOpen) {
        if (nIds >= 4) break;
        if (nIds > 0) clean += ',';
        tokenOpen = true;
        nIds++;
      }
      clean += ch;
    } else if (ch == ',') {
      tokenOpen = false;
    }
  }
  pluginsSetCryptoIds(clean);   // vide autorisé = liste effacée
  server.send(200, "text/plain", "ok");
}

// Config d'affichage + données live pour la SPA (aperçu LED, toggles, ordre).
static void handleApiDisplay() {
  const ScreenSettings& ss = settingsGetScreens();
  const SensorSettings& sc = settingsGetSensors();
  const ThresholdsCO2& th = settingsGetThresholdsCO2();
  const PluginsConfig& pc = pluginsCfg();
  const WeatherData& wd = pluginsWeather();
  const CryptoData& cd = pluginsCrypto();

  JsonDocument d;
  d["rotation"] = settingsGetRotationSec();

  JsonArray ord = d["order"].to<JsonArray>();
  const String& order = settingsGetScreenOrder();
  int start = 0;
  while (start < (int)order.length()) {
    int comma = order.indexOf(',', start);
    if (comma < 0) comma = order.length();
    ord.add(order.substring(start, comma));
    start = comma + 1;
  }

  JsonObject en = d["enabled"].to<JsonObject>();
  en["clock"] = ss.clock;  en["weather"] = ss.weather;  en["crypto"] = ss.crypto;
  en["pm1"] = ss.pm1;      en["pm25"] = ss.pm25;        en["pm10"] = ss.pm10;
  en["co2"] = ss.co2;      en["temp"] = ss.temp;        en["humi"] = ss.humi;
  en["tvoc"] = ss.tvoc;    en["hcho"] = ss.hcho;
  en["logo_ma"] = ss.logo_moduleair;
  en["logo_ac"] = ss.logo_aircarto;
  en["logo_as"] = ss.logo_atmosud;
#ifdef BUILD_LAIRETMOI
  en["logo_lam"] = ss.logo_lairetmoi;
#endif

  JsonObject sens = d["sensors"].to<JsonObject>();
  sens["npm"] = sc.npm_enabled;
  sens["mhz19"] = sc.mhz19_enabled;
  sens["bme280"] = sc.bme280_enabled;
  sens["ccs811"] = sc.ccs811_enabled;
  sens["sfa40"] = sc.sfa40_enabled;

  d["brightness"] = displayGetBrightness();
  d["language"] = i18nGetLang() == LANG_EN ? "EN" : "FR";
  d["debugSplash"] = displayGetDebugSplash();
  d["co2_good"] = th.good;
  d["co2_bad"] = th.bad;
  d["timeValid"] = pluginsTimeValid();

  JsonObject w = d["weather"].to<JsonObject>();
  w["city"] = pc.wxCity;
  w["lat"] = pc.wxLat;
  w["lon"] = pc.wxLon;
  w["valid"] = wd.valid;
  if (wd.valid) {
    w["temp"] = wd.temp;
    w["code"] = wd.code;
    w["max"] = wd.tmax;
    w["min"] = wd.tmin;
  }

  JsonObject cj = d["crypto"].to<JsonObject>();
  cj["ids"] = pc.cryptoIds;
  JsonArray items = cj["items"].to<JsonArray>();
  if (cd.valid) {
    for (int i = 0; i < cd.count; i++) {
      JsonObject it = items.add<JsonObject>();
      it["sym"] = cd.items[i].sym;
      it["price"] = cd.items[i].price;
      it["chg"] = cd.items[i].change;
    }
  }

  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void handleSetLang() {
  String lang = server.arg("lang");
  if (lang == "EN")      i18nSetLang(LANG_EN);
  else if (lang == "FR") i18nSetLang(LANG_FR);
  else { server.send(400, "text/plain", "invalid lang"); return; }
  Logger.printf("[Web] Language set to %s\n", lang.c_str());
  server.send(200, "text/plain", "ok");
}

static void handleSetThresholdsCO2() {
  int good = server.arg("good").toInt();
  int bad = server.arg("bad").toInt();
  if (good > 0 && bad > good) {
    settingsSetThresholdsCO2(good, bad);
    server.send(200, "text/plain", "ok");
  } else {
    server.send(400, "text/plain", "invalid: good must be < bad");
  }
}

static void handleSetBrightness() {
  uint8_t val = (uint8_t)server.arg("val").toInt();
  // val 0 = screen off (allowed)
  displaySetBrightness(val);
  server.send(200, "text/plain", "ok");
}

static void handleDebugSplash() {
  bool enabled = server.arg("enabled") == "1";
  displaySetDebugSplash(enabled);
  Logger.printf("[Web] Debug splash %s\n", enabled ? "enabled" : "disabled");
  server.send(200, "text/plain", "ok");
}

// =============================================

static void handleLogs() {
  // Tail incremental : le client passe ?since=N (dernier seq qu'il a vu),
  // on renvoie uniquement les nouvelles lignes + le seq courant en header.
  uint32_t since = 0;
  if (server.hasArg("since")) since = (uint32_t)strtoul(server.arg("since").c_str(), nullptr, 10);

  uint32_t currentSeq = loggerCurrentSeq();
  server.sendHeader("X-Log-Seq", String(currentSeq));
  server.sendHeader("Access-Control-Expose-Headers", "X-Log-Seq");

  // Streaming chunked : evite de construire une grosse String en heap.
  // PIEGE chunked : sendContent("") emet un chunk de taille 0, qui signifie
  // FIN DE REPONSE en HTTP chunked (c'est le terminateur volontaire plus bas).
  // L'ancien code faisait sendContent(line) puis sendContent("\n") : a la
  // premiere ligne VIDE du buffer (les Logger.println() entre les blocs), la
  // reponse se terminait la. Symptomes : le tail web s'arretait avant les
  // blocs [AirCarto]/[AtmoSud], et le module continuait d'ecrire dans un
  // socket que le client venait de fermer -> flood "errno: 104 Connection
  // reset by peer". D'ou UNE seule ecriture par ligne, '\n' inclus : une
  // ligne vide part comme "\n" (chunk de 1 octet), jamais comme chunk vide.
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain", "");
  loggerForEachLineSince(since, [](uint32_t seq, const char* line) {
    String out(line);
    out += '\n';
    server.sendContent(out);
  });
  server.sendContent("");  // termine le transfert chunked
}

// =============================================
// OTA Update
// =============================================

static int compareVersions(const String& a, const String& b) {
  int aMaj = 0, aMin = 0, aPat = 0;
  int bMaj = 0, bMin = 0, bPat = 0;
  sscanf(a.c_str(), "%d.%d.%d", &aMaj, &aMin, &aPat);
  sscanf(b.c_str(), "%d.%d.%d", &bMaj, &bMin, &bPat);
  if (aMaj != bMaj) return aMaj - bMaj;
  if (aMin != bMin) return aMin - bMin;
  return aPat - bPat;
}

// Traduit un code d'echec HTTPClient en explication lisible (FR) pour l'UI et
// les logs. Les codes <= 0 sont des erreurs de la lib (connexion jamais
// etablie) ; les codes > 0 sont des reponses HTTP du serveur.
static String otaFailureReason(int code) {
  bool en = (i18nGetLang() == LANG_EN);
  switch (code) {
    case -1:  return en ? "TLS connection failed (handshake error). Most common cause: low memory (fragmented heap) at check time, or a TLS server that didn't respond."
                        : "Connexion TLS impossible (handshake echoue). Cause la plus frequente : manque de memoire (heap fragmente) au moment du check, ou serveur TLS qui n'a pas repondu.";
    case -2:  return en ? "Failed to send the HTTP header." : "Envoi de l'en-tete HTTP echoue.";
    case -3:  return en ? "Failed to send the request." : "Envoi de la requete echoue.";
    case -4:  return en ? "Not connected to the server (connection lost before the request)." : "Pas connecte au serveur (connexion perdue avant la requete).";
    case -5:  return en ? "Connection lost during the exchange." : "Connexion perdue pendant l'echange.";
    case -7:  return en ? "No HTTP server at the other end." : "Pas de serveur HTTP a l'autre bout.";
    case -8:  return en ? "Not enough memory (RAM) for the request." : "Memoire insuffisante (RAM) pour la requete.";
    case -11: return en ? "Read timeout: the server took too long to respond." : "Timeout de lecture : le serveur a mis trop de temps a repondre.";
    default:
      if (code > 0) return en ? "The server replied HTTP " + String(code) + " (expected: 200)."
                              : "Le serveur a repondu HTTP " + String(code) + " (attendu : 200).";
      return en ? "Network error (code " + String(code) + ")." : "Erreur reseau (code " + String(code) + ").";
  }
}

static void handleCheckUpdate() {
  Logger.println("[OTA] Checking for update...");

  String versionUrl = String(OTA_UPDATE_URL) + "/version.txt?sensor=" + deviceId + "&current_version=" + FIRMWARE_VERSION;

  int httpCode = 0;
  String remoteVersion;
  uint32_t lastHeap = 0, lastMaxBlock = 0;  // etat du tas au dernier echec

  // Le handshake TLS (mbedTLS) reclame un gros bloc de heap CONTIGU (~40 Ko).
  // Sur ESP32 il echoue parfois au 1er essai avec HTTP -1
  // (HTTPC_ERROR_CONNECTION_REFUSED = connexion jamais etablie) a cause de la
  // fragmentation du tas ou d'un alea reseau, puis passe au 2e essai. On
  // reessaie donc jusqu'a 3 fois. En cas d'echec on logue l'erreur exacte ET
  // l'etat du tas (heap libre + plus gros bloc allouable) : si maxBlock tombe
  // sous ~35 Ko, c'est la penurie memoire qui tue le handshake, pas le reseau.
  const int MAX_ATTEMPTS = 3;
  for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    WiFiClientSecure secClient;
    secClient.setInsecure();          // Skip cert validation (simplifies deployment)
    secClient.setHandshakeTimeout(15); // s — borne le handshake TLS

    HTTPClient http;
    http.begin(secClient, versionUrl);
    http.setConnectTimeout(10000);
    http.setTimeout(10000);
    http.setReuse(false);

    httpCode = http.GET();

    if (httpCode == 200) {
      remoteVersion = http.getString();
      remoteVersion.trim();
      http.end();
      break;
    }

    lastHeap = ESP.getFreeHeap();
    lastMaxBlock = ESP.getMaxAllocHeap();
    Logger.printf("[OTA] Tentative %d/%d echouee: HTTP %d (%s) | heap=%u o, maxBloc=%u o\n",
                  attempt, MAX_ATTEMPTS, httpCode,
                  http.errorToString(httpCode).c_str(),
                  lastHeap, lastMaxBlock);
    http.end();

    if (attempt < MAX_ATTEMPTS) delay(400);  // laisse le heap/reseau respirer
  }

  if (httpCode != 200) {
    String reason = otaFailureReason(httpCode);
    String detail = "Code " + String(httpCode) + " | RAM libre " + String(lastHeap / 1024)
                  + " Ko, plus gros bloc " + String(lastMaxBlock / 1024) + " Ko";
    // Trace complete cote logs (UI /logs + serial) pour qu'on sache pourquoi.
    Logger.printf("[OTA] Version check failed apres %d tentatives — %s\n", MAX_ATTEMPTS, reason.c_str());
    Logger.println("[OTA] Detail: " + detail);
    if (lastMaxBlock > 0 && lastMaxBlock < 35000) {
      Logger.println("[OTA] -> plus gros bloc < 35 Ko : le handshake TLS manque de memoire (heap fragmente).");
    }
    // Echappe les guillemets eventuels avant injection JSON.
    reason.replace("\"", "'");
    detail.replace("\"", "'");
    server.send(200, "application/json",
      "{\"error\":\"" + reason + "\",\"detail\":\"" + detail + "\",\"code\":" + String(httpCode) + "}");
    return;
  }

  Logger.printf("[OTA] Current: %s, Remote: %s\n", FIRMWARE_VERSION, remoteVersion.c_str());

  if (compareVersions(remoteVersion, FIRMWARE_VERSION) > 0) {
    server.send(200, "application/json",
      "{\"update\":true,\"current\":\"" FIRMWARE_VERSION "\",\"remote\":\"" + remoteVersion + "\"}");
  } else {
    server.send(200, "application/json",
      "{\"update\":false,\"current\":\"" FIRMWARE_VERSION "\",\"remote\":\"" + remoteVersion + "\"}");
  }
}

static void handleDoUpdate() {
  Logger.println("[OTA] Starting firmware update...");
  server.send(200, "application/json", "{\"ok\":true}");
  delay(500);  // Laisser le temps d'envoyer la réponse

  displayShowOtaUpdate();

  // Disable WiFi modem sleep for the duration of the OTA: default
  // WIFI_PS_MIN_MODEM lets the radio nap between DTIM beacons, adding
  // ~100-300 ms of latency per packet which crushes HTTPS bulk download
  // throughput (we saw ~1.5 KB/s instead of the expected 30-80 KB/s).
  WiFi.setSleep(false);

  // Unmount SPIFFS so the OTA partition write isn't competing with logo
  // reads on the single SPI flash bus. Each SPIFFS read can stall the
  // OTA writer by 30-100 ms. SPIFFS comes back at reboot (success path)
  // or is remounted below on failure.
  SPIFFS.end();

  WiFiClientSecure secClient;
  secClient.setInsecure();  // Skip certificate validation
  // Note: setBufferSizes() is an ESP8266/BearSSL API; on ESP32 mbedTLS the
  // TLS record buffer is fixed at 16 KB internally, so there's nothing to
  // tune here. Just raise the read timeout to survive TLS slow start.
  secClient.setTimeout(60000);  // 60s — HTTPUpdate's internal 8s default trips on slow start
  String firmwareUrl = String(OTA_UPDATE_URL) + "/firmware.bin?sensor=" + deviceId + "&current_version=" + FIRMWARE_VERSION;

  Logger.printf("[OTA] Downloading from: %s\n", firmwareUrl.c_str());

  httpUpdate.rebootOnUpdate(false);  // Don't auto-reboot, we handle it manually
  httpUpdate.onProgress([](int current, int total) {
    if (total > 0) {
      int pct = (current * 100) / total;
      displayShowOtaProgress(pct);
      // Only log on 10% boundaries so the serial output doesn't add overhead
      static int lastLogged = -1;
      int bucket = pct / 10;
      if (bucket != lastLogged) {
        lastLogged = bucket;
        Logger.printf("[OTA] Progress: %d%%\n", pct);
      }
    }
  });

  t_httpUpdate_return ret = httpUpdate.update(secClient, firmwareUrl);

  switch (ret) {
    case HTTP_UPDATE_OK:
      Logger.println("[OTA] Update successful! Rebooting...");
      displayShowOtaDone();
      delay(2000);
      ESP.restart();
      break;
    case HTTP_UPDATE_FAILED:
      Logger.printf("[OTA] Update failed: %s\n", httpUpdate.getLastErrorString().c_str());
      displayShowOtaFailed();
      // Restore baseline behaviour so the device keeps working after a failed OTA
      WiFi.setSleep(true);
      SPIFFS.begin(true);
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Logger.println("[OTA] No update available");
      WiFi.setSleep(true);
      SPIFFS.begin(true);
      break;
  }
}


static void handleApiInfo() {
  const SensorSettings& sc = settingsGetSensors();
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  String ssid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : apSSID;

  String sensorsJson = "[";
  bool firstSensor = true;
  auto addSensor = [&](const char* name, bool enabled) {
    if (!enabled) return;
    if (!firstSensor) sensorsJson += ',';
    firstSensor = false;
    sensorsJson += '"'; sensorsJson += name; sensorsJson += '"';
  };
  addSensor("npm", sc.npm_enabled);
  addSensor("mhz19", sc.mhz19_enabled);
  addSensor("bme280", sc.bme280_enabled);
  addSensor("ccs811", sc.ccs811_enabled);
  addSensor("sfa40", sc.sfa40_enabled);
  sensorsJson += ']';

  String json = "{";
  bool first = true;
  jsonAddStringField(json, "chipId", deviceId, first);
  jsonAddStringField(json, "version", FIRMWARE_VERSION, first);
  jsonAddStringField(json, "wifiState", apiWifiStateName(), first);
  jsonAddStringField(json, "ssid", ssid, first);
  jsonAddStringField(json, "hostname", apSSID, first);
  jsonAddStringField(json, "ip", ip, first);
  jsonAddIntField(json, "rssi", WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0, first);
  jsonAddIntField(json, "uptime", millis() / 1000, first);
  jsonAddStringField(json, "latitude", "", first);
  jsonAddStringField(json, "longitude", "", first);
  if (!first) json += ',';
  json += "\"sensors\":" + sensorsJson;
  json += '}';
  server.send(200, "application/json", json);
}

static void handleApiConfig() {
  const SensorSettings& sc = settingsGetSensors();
  const ScreenSettings& ss = settingsGetScreens();
  const ThresholdsCO2& co2th = settingsGetThresholdsCO2();

  String json = "{";
  bool first = true;
  jsonAddStringField(json, "wlanssid", WiFi.SSID(), first);
  jsonAddBoolField(json, "has_wifi", WiFi.status() == WL_CONNECTED, first);
  jsonAddBoolField(json, "has_lora", false, first);
  jsonAddStringField(json, "latitude", "", first);
  jsonAddStringField(json, "longitude", "", first);

  jsonAddBoolField(json, "npm_read", sc.npm_enabled, first);
  jsonAddBoolField(json, "mhz19_read", sc.mhz19_enabled, first);
  jsonAddBoolField(json, "bme280_read", sc.bme280_enabled, first);
  jsonAddBoolField(json, "bmx280_read", sc.bme280_enabled, first);
  jsonAddBoolField(json, "mhz16_read", false, first);
  // s88_read : champ legacy Next-Gen, jusqu'ici cable en dur a false. Il reflete
  // desormais la realite — la voie CO2 est active ET c'est bien une SenseAir qui
  // a repondu a l'auto-detection.
  {
    const char* det = sensorsGetCo2SensorName();
    jsonAddBoolField(json, "s88_read", sc.mhz19_enabled && sensorsCo2DetectedIsS88(), first);
    jsonAddStringField(json, "co2_sensor_detected", det ? det : "", first);
  }
  jsonAddBoolField(json, "ccs811_read", sc.ccs811_enabled, first);
  jsonAddBoolField(json, "sfa40_read", sc.sfa40_enabled, first);
  jsonAddBoolField(json, "nebuleair_read", false, first);
  jsonAddStringField(json, "nebuleair_id", "", first);

  jsonAddBoolField(json, "has_matrix", true, first);
  jsonAddBoolField(json, "has_ssd1306", false, first);
  jsonAddBoolField(json, "display_measure", true, first);
  jsonAddBoolField(json, "display_forecast", false, first);

  jsonAddBoolField(json, "screen_pm01", ss.pm1, first);
  jsonAddBoolField(json, "screen_pm25", ss.pm25, first);
  jsonAddBoolField(json, "screen_pm10", ss.pm10, first);
  jsonAddBoolField(json, "screen_co2", ss.co2, first);
  jsonAddBoolField(json, "screen_cov", ss.tvoc, first);
  jsonAddBoolField(json, "screen_temp", ss.temp, first);
  jsonAddBoolField(json, "screen_humi", ss.humi, first);
  jsonAddBoolField(json, "screen_press", false, first);
  jsonAddBoolField(json, "screen_hcho", ss.hcho, first);
  jsonAddBoolField(json, "logo_moduleair", ss.logo_moduleair, first);
  jsonAddBoolField(json, "logo_aircarto", ss.logo_aircarto, first);
  jsonAddBoolField(json, "logo_atmosud", ss.logo_atmosud, first);
#ifdef BUILD_LAIRETMOI
  jsonAddBoolField(json, "logo_lairetmoi", ss.logo_lairetmoi, first);
#else
  jsonAddBoolField(json, "logo_lairetmoi", false, first);
#endif

  jsonAddBoolField(json, "screen_clock", ss.clock, first);
  jsonAddBoolField(json, "screen_weather", ss.weather, first);
  jsonAddBoolField(json, "screen_crypto", ss.crypto, first);
  jsonAddStringField(json, "screen_order", settingsGetScreenOrder(), first);
  jsonAddIntField(json, "screen_rotation_s", settingsGetRotationSec(), first);

  jsonAddIntField(json, "display_brightness", displayGetBrightness(), first);
  jsonAddBoolField(json, "debug_splash", displayGetDebugSplash(), first);
  jsonAddStringField(json, "language", i18nGetLang() == LANG_EN ? "EN" : "FR", first);
  jsonAddIntField(json, "co2_good", co2th.good, first);
  jsonAddIntField(json, "co2_bad", co2th.bad, first);

  jsonAddBoolField(json, "send2custom", false, first);
  jsonAddBoolField(json, "send2dusti", false, first);
  jsonAddIntField(json, "sending_intervall_ms", DATA_SEND_INTERVAL, first);
  jsonAddStringField(json, "temp_offset", "0", first);
  json += '}';
  server.send(200, "application/json", json);
}

static void handleApiWifi() {
  Logger.println("[API] /api/wifi - launching WiFi scan");
  int n = WiFi.scanNetworks();
  if (n < 0) n = 0;

  String json = "{\"count\":" + String(n) + ",\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ',';
    json += "{\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i));
    json += ",\"channel\":" + String(WiFi.channel(i));
    json += ",\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]}";

  WiFi.scanDelete();
  scanCount = -1;
  scanInProgress = false;
  server.send(200, "application/json", json);
}

static void addSensorValue(String& json, bool& first, const char* type, const String& value) {
  if (!first) json += ',';
  first = false;
  json += "{\"value_type\":\"";
  json += type;
  json += "\",\"value\":\"";
  json += jsonEscape(value);
  json += "\"}";
}

static void handleDataJson() {
  const SensorData& d = sensorsGetData();
  String json = "{\"software_version\":\"" FIRMWARE_VERSION "\"";
  if (d.lastReadTime > 0) {
    json += ",\"age\":\"" + String((millis() - d.lastReadTime) / 1000) + "\"";
  }
  json += ",\"sensordatavalues\":[";

  bool first = true;
  if (d.pm_ok) {
    addSensorValue(json, first, "NPM_P0", String(d.pm1, 1));
    addSensorValue(json, first, "NPM_P1", String(d.pm10, 1));
    addSensorValue(json, first, "NPM_P2", String(d.pm25, 1));
  }
  if (d.bme_ok) {
    addSensorValue(json, first, "BME280_temperature", String(d.temperature, 1));
    addSensorValue(json, first, "BME280_humidity", String(d.humidity, 1));
    addSensorValue(json, first, "BME280_pressure", String(d.pressure * 100.0f, 1));
  }
  if (d.co2_ok) {
    addSensorValue(json, first, "MHZ19_CO2", String(d.co2));
  }
  if (d.ccs_ok) {
    addSensorValue(json, first, "CCS811", String(d.tvoc));
  }
  if (d.sfa40_ok) {
    addSensorValue(json, first, "SFA40_HCHO", String(d.hcho, 1));
  }
  if (WiFi.status() == WL_CONNECTED) {
    addSensorValue(json, first, "signal", String(WiFi.RSSI()));
  }

  json += "]}";
  server.send(200, "application/json", json);
}

// =============================================
// Init & Loop
// =============================================

void wifiManagerInit() {
  preferences.begin("wifi", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  preferences.end();

  // First boot (or after a WiFi reset): NVS holds no SSID. Fall back to the
  // compile-time default credentials so a factory-fresh board auto-joins the
  // lab network. The defaults are transient — we don't write them to NVS, so
  // a user-configured network always wins on later boots. If the default
  // network isn't reachable, the connect attempt below fails and we drop into
  // the normal AP-config flow, exactly as if no creds existed.
  if (ssid.length() == 0 && strlen(DEFAULT_WIFI_SSID) > 0) {
    ssid = DEFAULT_WIFI_SSID;
    password = DEFAULT_WIFI_PASSWORD;
    Logger.printf("[WiFi] No saved creds — trying default SSID '%s'\n", ssid.c_str());
  }

  if (ssid.length() > 0) {
    Logger.printf("[WiFi] Saved SSID found: %s\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(MDNS_NAME);
    WiFi.onEvent(onWifiEvent);
    displayShowWifiConnecting(ssid.c_str());

    // Tentative 1
    Logger.printf("[WiFi] Tentative 1/%d (timeout %ds)...\n", WIFI_MAX_ATTEMPTS, WIFI_CONNECT_TIMEOUT / 1000);
    bool connected = wifiTryConnect(ssid, password, WIFI_CONNECT_TIMEOUT);

    // Si echec : decider via scan + classification reason si on retry ou pas
    if (!connected) {
      WifiFailGroup g = classifyReason(lastDisconnectReason);
      Logger.printf("[WiFi] Echec tentative 1 (reason=%u, %s)\n",
                    lastDisconnectReason, failGroupLabel(g));

      if (g == WFG_AUTH) {
        Logger.println("[WiFi] -> mode AP direct (mdp probablement faux, retry inutile)");
      } else if (!wifiSsidIsVisible(ssid)) {
        Logger.println("[WiFi] -> mode AP direct (SSID absent, capteur deplace ou AP eteint)");
      } else {
        // SSID present + echec non-auth -> probablement transitoire, on retry
        Logger.printf("[WiFi] SSID visible, retry... (tentative 2/%d)\n", WIFI_MAX_ATTEMPTS);
        connected = wifiTryConnect(ssid, password, WIFI_CONNECT_TIMEOUT);
        if (!connected) {
          g = classifyReason(lastDisconnectReason);
          Logger.printf("[WiFi] Echec tentative 2 (reason=%u, %s) -> mode AP\n",
                        lastDisconnectReason, failGroupLabel(g));
        }
      }
    }

    if (connected) {
      setWifiState(WS_STA_CONNECTED);
      Logger.printf("[WiFi] Connected!\n");
      Logger.printf("[WiFi] IP:      %s\n", WiFi.localIP().toString().c_str());
      Logger.printf("[WiFi] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
      Logger.printf("[WiFi] DNS:     %s\n", WiFi.dnsIP().toString().c_str());
      Logger.printf("[WiFi] MAC:     %s\n", WiFi.macAddress().c_str());
      Logger.printf("[WiFi] RSSI:    %d dBm\n", WiFi.RSSI());
      Logger.printf("[WiFi] Channel: %d\n", WiFi.channel());
      displayShowWifiConnected(ssid.c_str(), WiFi.RSSI());
      delay(3000);
      displayShowLogo();
    } else {
      // Show WHY the connect failed before the AP-mode splash replaces it
      // (previously the screen jumped straight from "Connexion" to "Config
      // WiFi" with no failure feedback). lastDisconnectReason is still set by
      // the event handler from the final attempt.
      WifiFailGroup fg = classifyReason(lastDisconnectReason);
      WifiFailReason fr = (fg == WFG_AUTH)         ? WIFI_FAIL_AUTH
                        : (fg == WFG_AP_NOT_FOUND) ? WIFI_FAIL_NO_AP
                                                   : WIFI_FAIL_OTHER;
      displayShowWifiFailed(ssid.c_str(), fr);
      delay(3000);
      Logger.println("[WiFi] Falling back to AP mode...");
    }
  } else {
    Logger.println("[WiFi] No saved credentials found");
  }

  if (wifiState != WS_STA_CONNECTED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID.c_str());   // open network — see config.h for rationale
    setWifiState(WS_AP_CONFIG);   // start the 3-min config-splash timer
    Logger.printf("[WiFi] AP started (open)\n");
    Logger.printf("[WiFi] SSID:     %s\n", apSSID.c_str());
    Logger.printf("[WiFi] AP IP:    %s\n", WiFi.softAPIP().toString().c_str());
    Logger.printf("[WiFi] AP MAC:   %s\n", WiFi.softAPmacAddress().c_str());
    displayShowAPMode(apSSID.c_str(), WiFi.softAPIP().toString().c_str());

    // DNS captive portal : toutes les requêtes DNS → IP de l'AP
    dnsServer.start(53, "*", WiFi.softAPIP());
    Logger.println("[DNS] Captive portal DNS started");

    // BLE Improv WiFi : configuration via Bluetooth (Chrome/Edge)
    bleImprovOnCredentials([](const String& ssid, const String& password) {
      wifiSaveCredentialsAndRestart(ssid, password);
    });
    bleImprovInit(apSSID);
  }

  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Logger.printf("[mDNS] http://%s.local\n", MDNS_NAME);
  } else {
    Logger.println("[mDNS] Failed to start");
  }

  server.on("/", handleRoot);
  server.on("/config", handleRootConnected);   // ancienne interface (secours)
  server.on("/api/info", handleApiInfo);
  server.on("/api/config", handleApiConfig);
  server.on("/api/display", handleApiDisplay);
  server.on("/api/wifi", handleApiWifi);
  server.on("/data.json", handleDataJson);
  server.on("/scan", handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/reboot", HTTP_POST, handleReboot);

  server.on("/logs", handleLogs);
  server.on("/debug-splash", HTTP_POST, handleDebugSplash);
  server.on("/set-brightness", HTTP_POST, handleSetBrightness);
  server.on("/set-thresholds-co2", HTTP_POST, handleSetThresholdsCO2);
  server.on("/set-sensor", HTTP_POST, handleSetSensor);
  server.on("/set-screen", HTTP_POST, handleSetScreen);
  server.on("/set-screen-order", HTTP_POST, handleSetScreenOrder);
  server.on("/set-rotation", HTTP_POST, handleSetRotation);
  server.on("/set-weather", HTTP_POST, handleSetWeather);
  server.on("/set-crypto", HTTP_POST, handleSetCrypto);
  server.on("/set-lang", HTTP_POST, handleSetLang);
  server.on("/check-update", handleCheckUpdate);
  server.on("/do-update", handleDoUpdate);

  // Captive portal detection endpoints (Android, iOS, Windows)
  auto redirect = []() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  };
  server.on("/generate_204", redirect);        // Android
  server.on("/gen_204", redirect);              // Android
  server.on("/hotspot-detect.html", redirect);  // iOS
  server.on("/connecttest.txt", redirect);      // Windows
  server.on("/fwlink", redirect);               // Windows

  server.onNotFound([]() {
    Logger.printf("[Web] 404: %s %s\n", server.method() == HTTP_GET ? "GET" : "POST", server.uri().c_str());
    if (isApActive()) {
      // Captive portal : rediriger toute requête inconnue vers /
      server.sendHeader("Location", "http://192.168.4.1/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "Not found");
    }
  });
  server.begin();
  Logger.println("[Web] Server started on port 80");

  // Lancer le premier scan en arrière-plan (uniquement en mode AP)
  if (isApActive()) {
    startScanIfNeeded();
  }
}

void wifiSaveCredentialsAndRestart(const String& ssid, const String& password) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.end();

  Logger.printf("[WiFi] Credentials saved - SSID: %s (via BLE)\n", ssid.c_str());
  Logger.println("[WiFi] Restarting in 2 seconds...");
  delay(2000);
  ESP.restart();
}

// Bring the SoftAP up and switch the FSM to WS_AP_CONFIG. Used both at boot
// (when the first STA attempt fails) and as the escalation step after a
// failed 3-minute STA reconnect window. Idempotent enough to call when the
// AP is already up — but normally only called on a real transition.
static void startApFallback() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str());   // open network
  Logger.printf("[WiFi] AP started (open): %s (IP %s)\n",
                apSSID.c_str(), WiFi.softAPIP().toString().c_str());
  displayShowAPMode(apSSID.c_str(), WiFi.softAPIP().toString().c_str());
  dnsServer.start(53, "*", WiFi.softAPIP());
  Logger.println("[DNS] Captive portal DNS started");
  setWifiState(WS_AP_CONFIG);
}

// Kick off a background reconnect attempt using the credentials in NVS.
// Non-blocking: WiFi.begin() returns immediately, the WiFi driver runs the
// association in its own task. We just switch state to WS_AP_RETRYING and
// the FSM tick polls WiFi.status() to detect success or timeout.
static void attemptBackgroundReconnect() {
  preferences.begin("wifi", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  preferences.end();

  if (ssid.length() == 0) {
    Logger.println("[WiFi] No saved creds, skipping background reconnect");
    setWifiState(WS_AP_DATA);   // resets stateEnteredAt so we wait 10 min again
    return;
  }

  Logger.printf("[WiFi] Background reconnect to '%s' (AP stays up)...\n", ssid.c_str());
  WiFi.mode(WIFI_AP_STA);
  lastDisconnectReason = 0;
  WiFi.begin(ssid.c_str(), password.c_str());
  setWifiState(WS_AP_RETRYING);
}

// Log AP-client transitions (a phone joining/leaving the hotspot).
static void apClientLogTick() {
  int clients = WiFi.softAPgetStationNum();
  if (clients != lastAPClients) {
    Logger.printf("[WiFi] AP clients: %d -> %d\n", lastAPClients, clients);
    lastAPClients = clients;
  }
}

void wifiManagerLoop() {
  // Fast path: serve pending HTTP/DNS requests and BLE events every loop.
  // DNS captive portal stays alive in every AP state (including WS_AP_DATA)
  // so a phone joining the AP at any time still hits the portal correctly.
  if (isApActive()) {
    dnsServer.processNextRequest();
    bleImprovLoop();
  }
  server.handleClient();

  // Drain async scan results.
  if (scanInProgress) {
    int result = WiFi.scanComplete();
    if (result != WIFI_SCAN_RUNNING) {
      scanCount = (result == WIFI_SCAN_FAILED) ? 0 : result;
      scanInProgress = false;
      Logger.printf("[WiFi] Scan complete: %d networks found\n", scanCount);
    }
  }

  // 1 Hz FSM tick. Throttled so we don't spam log + WiFi APIs.
  static unsigned long lastTick = 0;
  unsigned long now = millis();
  if (now - lastTick < 1000) return;
  lastTick = now;

  switch (wifiState) {
    case WS_STA_CONNECTED:
      // Invariant : le BLE Improv ne sert qu'au provisioning, il n'a JAMAIS
      // le droit de tourner en STA connecte (cf. teardown AP plus haut). Ce
      // filet couvre tout chemin — present ou futur — qui atteindrait cet
      // etat sans passer par le teardown explicite. No-op si BLE arrete.
      bleImprovStop();
      // First detection of a dropped association: enter the 3-min recovery
      // window rather than spamming reconnect() on every 1 Hz tick (which
      // was the old behaviour — it filled /logs with hundreds of
      // "Connection lost" lines and never escalated to AP fallback).
      if (WiFi.status() != WL_CONNECTED) {
        Logger.println("[WiFi] Connection lost — entering 3 min reconnect window");
        WiFi.reconnect();
        lastReconnectKickAt = now;
        setWifiState(WS_STA_RECONNECTING);
      }
      break;

    case WS_STA_RECONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        // Recovered without needing the AP fallback.
        Logger.printf("[WiFi] Reconnected to %s (RSSI %d dBm)\n",
                      WiFi.SSID().c_str(), WiFi.RSSI());
        setWifiState(WS_STA_CONNECTED);
        displaySetNetStatus(NET_OK);
      } else if (now - stateEnteredAt > STA_RECONNECT_WINDOW_MS) {
        // 3 min elapsed without the driver getting back in — give up on
        // STA, bring up the AP/captive portal so the user can act, and
        // let the standard AP retry cycle (every 10 min) take over.
        Logger.println("[WiFi] 3 min reconnect window expired — falling back to AP");
        WiFi.disconnect(true);
        delay(100);
        startApFallback();
      } else if (now - lastReconnectKickAt > STA_RECONNECT_KICK_MS) {
        // Re-kick the driver every 30s in case its internal auto-retry
        // backed off. One log line per kick — no per-tick spam.
        Logger.println("[WiFi] Still reconnecting (kick)");
        WiFi.reconnect();
        lastReconnectKickAt = now;
      }
      break;

    case WS_AP_CONFIG:
      apClientLogTick();
      // After 3 min on the "Config WiFi" splash, switch the matrix to normal
      // data display. The AP keeps running so the user can still configure.
      if (now - stateEnteredAt > AP_CONFIG_DURATION_MS) {
        Logger.println("[WiFi] 3 min in AP_CONFIG — matrix switches to data display");
        setWifiState(WS_AP_DATA);
      }
      break;

    case WS_AP_DATA:
      apClientLogTick();
      // Every 10 min, attempt to reconnect to the saved WiFi in case the AP
      // came back online (router rebooted, came back from vacation, etc.).
      if (now - stateEnteredAt > AP_RETRY_INTERVAL_MS) {
        attemptBackgroundReconnect();
      }
      break;

    case WS_AP_RETRYING:
      if (WiFi.status() == WL_CONNECTED) {
        // Background reconnect succeeded — tear down AP gracefully and
        // switch to STA-only. No reboot, no display flash.
        Logger.println("[WiFi] Background reconnect SUCCEEDED");
        Logger.printf("[WiFi] IP: %s, RSSI: %d dBm, channel %d\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI(), WiFi.channel());
        delay(500);  // let any in-flight HTTP response flush over the AP
        WiFi.softAPdisconnect(true);
        dnsServer.stop();
        // Le BLE Improv demarre avec le mode AP ; il doit mourir avec lui.
        // Laisse tourner, il confisque ~50 Ko de heap et le handshake mbedTLS
        // (~40 Ko contigus) echoue sur TOUS les envois HTTPS en "connection
        // refused" — panne silencieuse vue sur le terrain apres une coupure
        // de courant (module boote avant la box -> fallback AP+BLE -> ce
        // chemin de reconnexion, le seul qui sort du mode AP sans reboot).
        bleImprovStop();
        WiFi.mode(WIFI_STA);
        setWifiState(WS_STA_CONNECTED);
        // Push the badge to OK instantly. The "Connecte" splash is drawn
        // by main.cpp on the next wasConnected/connected transition and
        // held for 3 s by the display suppression timer, after which the
        // rotation resumes — the badge will already say NET_OK then.
        displaySetNetStatus(NET_OK);
      } else if (now - stateEnteredAt > AP_RETRY_TIMEOUT_MS) {
        // 30 s elapsed without a successful association — abandon and go
        // back to AP_DATA. The 10-min counter restarts from now.
        WifiFailGroup g = classifyReason(lastDisconnectReason);
        Logger.printf("[WiFi] Background reconnect timeout (reason=%u, %s)\n",
                      lastDisconnectReason, failGroupLabel(g));
        WiFi.disconnect(false, false);   // drop STA part, keep AP
        WiFi.mode(WIFI_AP);
        setWifiState(WS_AP_DATA);
      }
      break;
  }
}

bool wifiIsConnected() {
  return wifiState == WS_STA_CONNECTED && WiFi.status() == WL_CONNECTED;
}

bool wifiIsApMode() {
  return isApActive();
}

bool wifiShouldShowConfigSplash() {
  return wifiState == WS_AP_CONFIG;
}
