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
#include "logger.h"

static WebServer server(80);
static DNSServer dnsServer;
static Preferences preferences;

static bool wifiConnected = false;
static bool apMode = false;
static int lastAPClients = 0;

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

static volatile uint8_t lastDisconnectReason = 0;

static void onWifiEvent(WiFiEvent_t e, WiFiEventInfo_t info) {
  if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastDisconnectReason = info.wifi_sta_disconnected.reason;
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
  b.disabled=true;b.textContent='Recherche...';
  fetch('/scan').then(r=>r.json()).then(function(nets){
    var h='<div class="scan-info">'+nets.length+' reseau(x) trouve(s)</div>';
    nets.forEach(function(w){
      var sig=w.rssi>-50?'&#9679;&#9679;&#9679;&#9679;':w.rssi>-60?'&#9679;&#9679;&#9679;&#9675;':w.rssi>-70?'&#9679;&#9679;&#9675;&#9675;':'&#9679;&#9675;&#9675;&#9675;';
      h+="<div class='wifi-item' onclick=\"selectWifi(this,'"+w.ssid+"')\">";
      h+="<span class='wifi-name'>"+w.ssid;
      if(w.encrypted)h+="<span class='lock'>&#128274;</span>";
      h+="</span><span class='wifi-rssi'>"+sig+" "+w.rssi+"dBm</span></div>";
      h+="<div class='wifi-form'><form action='/save' method='POST'>";
      h+="<input type='hidden' name='ssid' value='"+w.ssid+"'>";
      if(w.encrypted)h+="<div class='pw-wrap'><input type='password' name='password' placeholder='Mot de passe'><button type='button' class='pw-toggle' onclick='togglePw(this)'>&#128065;</button></div>";
      else h+="<input type='hidden' name='password' value=''>";
      h+="<button type='submit'>Connecter</button></form></div>";
    });
    if(nets.length==0)h='<div class="scan-info">Aucun reseau trouve</div>';
    c.innerHTML=h;
    b.disabled=false;b.textContent='Actualiser les reseaux';
  }).catch(function(){
    b.disabled=false;b.textContent='Actualiser les reseaux';
  });
}
function checkUpdate(){
  var d=document.getElementById('ota-status');
  var b=document.getElementById('ota-btn');
  d.innerHTML="<span class='data-label'>Verification en cours...</span>";
  fetch('/check-update').then(r=>r.json()).then(function(j){
    if(j.update){
      d.innerHTML="<span class='data-value warn'>Nouvelle version disponible : v"+j.remote+"</span>";
      b.textContent="Mettre a jour vers v"+j.remote;
      b.onclick=function(){doUpdate()};
      b.style.display='block';
    }else if(j.error){
      d.innerHTML="<span class='data-value bad'>Erreur : "+j.error+"</span>";
    }else{
      d.innerHTML="<span class='data-value good'>Firmware a jour (v"+j.current+")</span>";
    }
  }).catch(function(){
    d.innerHTML="<span class='data-value bad'>Erreur de connexion</span>";
  });
}
function doUpdate(){
  if(!confirm('Lancer la mise a jour ? Le capteur va redemarrer.'))return;
  var d=document.getElementById('ota-status');
  var b=document.getElementById('ota-btn');
  b.style.display='none';
  d.innerHTML="<span class='data-label'>Telechargement et installation en cours...<br>Ne pas eteindre le capteur !</span>";
  fetch('/do-update').then(r=>r.json()).then(function(j){
    if(j.ok){
      d.innerHTML="<span class='data-value good'>Mise a jour reussie ! Redemarrage...</span>";
    }else{
      d.innerHTML="<span class='data-value bad'>Echec : "+j.error+"</span>";
      b.style.display='block';
    }
  }).catch(function(){
    d.innerHTML="<span class='data-value good'>Mise a jour en cours, le capteur redémarre...</span>";
  });
}
)";

// =============================================
// Pages Web (envoi par chunks)
// =============================================

static void sendHeader() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  server.sendContent("<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ModuleAir</title><style>");
  server.sendContent(CSS);
  server.sendContent("</style><script>");
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
  if (days > 0) snprintf(buf, sizeof(buf), "%luj %02luh%02lu", days, hrs, min);
  else if (hrs > 0) snprintf(buf, sizeof(buf), "%luh%02lum%02lu", hrs, min, sec);
  else snprintf(buf, sizeof(buf), "%lum%02lus", min, sec);
  return String(buf);
}

static void handleRootConnected() {
  sendHeader();

  server.sendContent(
    "<div class='header'>"
    "<h1>ModuleAir</h1>"
    "<div class='version'>Firmware v" FIRMWARE_VERSION "</div>"
    "<button class='refresh-btn' onclick='location.reload()'>&#8635; Rafraichir</button>"
    "</div>");

  const SensorData& d = sensorsGetData();
  unsigned long ago = (d.lastReadTime > 0) ? (millis() - d.lastReadTime) / 1000 : 0;

  server.sendContent("<div class='grid'>");

  // ── Banniere d'alertes (visible uniquement s'il y a des erreurs) ──
  // On ne pollue pas l'UI quand tout va bien : la banniere n'apparait
  // que si au moins un capteur active est en erreur.
  {
    const SensorSettings& sc = settingsGetSensors();
    bool hasAlerts =
        (!d.pm_ok    && sc.npm_enabled)    ||
        (!d.co2_ok   && sc.mhz19_enabled)  ||
        (!d.bme_ok   && sc.bme280_enabled) ||
        (!d.ccs_ok   && sc.ccs811_enabled) ||
        (!d.sfa40_ok && sc.sfa40_enabled);

    if (hasAlerts) {
      String alerts = "<div class='card wide' style='background:#3e1a1a;border-left:4px solid #ef5350'>"
                      "<h2 style='color:#ef9a9a;display:flex;align-items:center;gap:10px;margin-bottom:8px'>"
                      "<span style='font-size:1.4em'>&#9888;</span> Alertes capteurs"
                      "</h2>";

      // NextPM — on decode npm_status pour donner la cause precise
      if (!d.pm_ok && sc.npm_enabled) {
        String detail;
        if (d.npmStatus == 0xFF) {
          detail = "Communication impossible (capteur muet)";
        } else if (d.npmStatus == 0) {
          detail = "Valeur aberrante ou lecture echouee";
        } else {
          const char* sep = "";
          if (d.npmStatus & 0x80) { detail += sep; detail += "laser HS";        sep = ", "; }
          if (d.npmStatus & 0x40) { detail += sep; detail += "memoire KO";      sep = ", "; }
          if (d.npmStatus & 0x20) { detail += sep; detail += "ventilateur HS";  sep = ", "; }
          if (d.npmStatus & 0x10) { detail += sep; detail += "T/H interne KO";  sep = ", "; }
          if (d.npmStatus & 0x08) { detail += sep; detail += "humidite excessive"; sep = ", "; }
          if (d.npmStatus & 0x04) { detail += sep; detail += "pas pret";        sep = ", "; }
          if (d.npmStatus & 0x02) { detail += sep; detail += "mode degrade";    sep = ", "; }
          if (d.npmStatus & 0x01) { detail += sep; detail += "en veille";       sep = ", "; }
          if (detail.length() == 0) detail = "status 0x" + String(d.npmStatus, HEX);
        }
        alerts += "<div class='data-row'><span class='data-label'>NextPM (PM)</span>"
                  "<span class='data-value bad' style='text-align:right'>" + detail + "</span></div>";
      }

      if (!d.co2_ok && sc.mhz19_enabled) {
        alerts += "<div class='data-row'><span class='data-label'>MH-Z19 (CO2)</span>"
                  "<span class='data-value bad' style='text-align:right'>Erreur de lecture (voir logs)</span></div>";
      }
      if (!d.bme_ok && sc.bme280_enabled) {
        alerts += "<div class='data-row'><span class='data-label'>BME280 (T/H/P)</span>"
                  "<span class='data-value bad' style='text-align:right'>Capteur introuvable</span></div>";
      }
      if (!d.ccs_ok && sc.ccs811_enabled) {
        alerts += "<div class='data-row'><span class='data-label'>CCS811 (COV)</span>"
                  "<span class='data-value bad' style='text-align:right'>Capteur introuvable</span></div>";
      }
      if (!d.sfa40_ok && sc.sfa40_enabled) {
        alerts += "<div class='data-row'><span class='data-label'>SFA40 (HCHO)</span>"
                  "<span class='data-value bad' style='text-align:right'>Capteur introuvable</span></div>";
      }

      alerts += "</div>";
      server.sendContent(alerts);
    }
  }

  // Status WiFi
  String chunk = "<div class='card span2'><h2>Connexion</h2>";
  chunk += "<div class='data-row'><span class='data-label'>Reseau</span><span class='data-value'>" + WiFi.SSID() + "</span></div>";
  chunk += "<div class='data-row'><span class='data-label'>IP</span><span class='data-value'>" + WiFi.localIP().toString() + "</span></div>";
  {
    int rssi = WiFi.RSSI();
    int bars = rssi > -50 ? 4 : rssi > -60 ? 3 : rssi > -70 ? 2 : 1;
    const char* label = bars == 4 ? "Excellent" : bars == 3 ? "Bon" : bars == 2 ? "Moyen" : "Faible";
    const char* color = bars >= 3 ? "#a5d6a7" : bars == 2 ? "#ffcc80" : "#ef9a9a";
    String svg = "<svg width='24' height='18' viewBox='0 0 24 18' style='vertical-align:middle;margin-right:6px;'>";
    for (int i = 0; i < 4; i++) {
      int h = 5 + i * 4;
      int y = 18 - h;
      String fill = (i < bars) ? String(color) : "#333";
      svg += "<rect x='" + String(i * 6) + "' y='" + String(y) + "' width='5' height='" + String(h) + "' rx='1' fill='" + fill + "'/>";
    }
    svg += "</svg>";
    chunk += "<div class='data-row'><span class='data-label'>Signal</span><span class='data-value' style='color:" + String(color) + "'>" + svg + label + " <span class='data-unit'>(" + String(rssi) + " dBm)</span></span></div>";
  }
  chunk += "<div class='data-row'><span class='data-label'>Uptime</span><span class='data-value'>" + formatUptime(millis()) + "</span></div>";
  chunk += "</div>";
  server.sendContent(chunk);

  // Particules fines
  chunk = "<div class='card'><h2>Particules fines (NextPM)</h2>";
  if (d.pm_ok) {
    chunk += "<div class='data-row'><span class='data-label'>PM1.0</span><span class='data-value'>" + String(d.pm1, 1) + " <span class='data-unit'>ug/m3</span></span></div>";
    chunk += "<div class='data-row'><span class='data-label'>PM2.5</span><span class='data-value " + String(d.pm25 < 15 ? "good" : d.pm25 < 35 ? "warn" : "bad") + "'>" + String(d.pm25, 1) + " <span class='data-unit'>ug/m3</span></span></div>";
    chunk += "<div class='data-row'><span class='data-label'>PM10</span><span class='data-value " + String(d.pm10 < 45 ? "good" : d.pm10 < 80 ? "warn" : "bad") + "'>" + String(d.pm10, 1) + " <span class='data-unit'>ug/m3</span></span></div>";
  } else {
    chunk += "<div class='scan-info'>En attente de donnees...</div>";
  }
  chunk += "</div>";
  server.sendContent(chunk);

  // CO2
  chunk = "<div class='card'><h2>CO2 (MH-Z19)</h2>";
  if (d.co2_ok) {
    String co2class = d.co2 < 800 ? "good" : d.co2 < 1200 ? "warn" : "bad";
    chunk += "<div class='data-row'><span class='data-label'>CO2</span><span class='data-value " + co2class + "'>" + String(d.co2) + " <span class='data-unit'>ppm</span></span></div>";
  } else {
    chunk += "<div class='scan-info'>En attente de donnees...</div>";
  }
  chunk += "</div>";
  server.sendContent(chunk);

  // COV (CCS811)
  chunk = "<div class='card'><h2>COV (CCS811)</h2>";
  if (d.ccs_ok) {
    String tvocClass = d.tvoc < 220 ? "good" : d.tvoc < 660 ? "warn" : "bad";
    String eco2Class = d.eco2 < 800 ? "good" : d.eco2 < 1200 ? "warn" : "bad";
    chunk += "<div class='data-row'><span class='data-label'>TVOC</span><span class='data-value " + tvocClass + "'>" + String(d.tvoc) + " <span class='data-unit'>ppb</span></span></div>";
    chunk += "<div class='data-row'><span class='data-label'>eCO2</span><span class='data-value " + eco2Class + "'>" + String(d.eco2) + " <span class='data-unit'>ppm</span></span></div>";
  } else {
    chunk += "<div class='scan-info'>En attente de donnees...</div>";
  }
  chunk += "</div>";
  server.sendContent(chunk);

  // Formaldéhyde (SFA40)
  chunk = "<div class='card'><h2>Formaldehyde (SFA40)</h2>";
  if (d.sfa40_ok) {
    String hchoClass = d.hcho < 30 ? "good" : d.hcho < 100 ? "warn" : "bad";
    chunk += "<div class='data-row'><span class='data-label'>HCHO</span><span class='data-value " + hchoClass + "'>" + String(d.hcho, 1) + " <span class='data-unit'>ppb</span></span></div>";
  } else {
    chunk += "<div class='scan-info'>En attente de donnees...</div>";
  }
  chunk += "</div>";
  server.sendContent(chunk);

  // Température / Humidité / Pression
  chunk = "<div class='card'><h2>Environnement (BME280)</h2>";
  if (d.bme_ok) {
    chunk += "<div class='data-row'><span class='data-label'>Temperature</span><span class='data-value'>" + String(d.temperature, 1) + " <span class='data-unit'>°C</span></span></div>";
    chunk += "<div class='data-row'><span class='data-label'>Humidite</span><span class='data-value'>" + String(d.humidity, 1) + " <span class='data-unit'>%</span></span></div>";
    chunk += "<div class='data-row'><span class='data-label'>Pression</span><span class='data-value'>" + String(d.pressure, 1) + " <span class='data-unit'>hPa</span></span></div>";
  } else {
    chunk += "<div class='scan-info'>En attente de donnees...</div>";
  }
  chunk += "</div>";
  server.sendContent(chunk);

  // Infos système
  chunk = "<div class='card'><h2>Systeme</h2>";
  chunk += "<div class='data-row'><span class='data-label'>Device ID</span><span class='data-value'>" + deviceId + "</span></div>";
  chunk += "<div class='data-row'><span class='data-label'>Firmware</span><span class='data-value'>v" FIRMWARE_VERSION "</span></div>";
  chunk += "<div class='data-row'><span class='data-label'>ESP32</span><span class='data-value'>" + String(ESP.getChipModel()) + " rev" + String(ESP.getChipRevision()) + "</span></div>";
  chunk += "<div class='data-row'><span class='data-label'>CPU</span><span class='data-value'>" + String(ESP.getCpuFreqMHz()) + " <span class='data-unit'>MHz</span></span></div>";
  chunk += "<div class='data-row'><span class='data-label'>RAM libre</span><span class='data-value'>" + String(ESP.getFreeHeap() / 1024) + " <span class='data-unit'>KB</span></span></div>";
  chunk += "<div class='data-row'><span class='data-label'>MAC</span><span class='data-value'>" + WiFi.macAddress() + "</span></div>";
  // Tri-state badge: ok / err / off. A user-disabled sensor must not look
  // like a failure — its badge is greyed out and struck through.
  {
    const SensorSettings& sc = settingsGetSensors();
    auto badgeClass = [](bool enabled, bool ok) {
      return !enabled ? "off" : (ok ? "ok" : "err");
    };
    chunk += "<div class='data-row'><span class='data-label'>Capteurs</span><span class='data-value'>";
    chunk += "<span class='sensor-badge " + String(badgeClass(sc.npm_enabled,    d.pm_ok))    + "'>NextPM</span>";
    chunk += "<span class='sensor-badge " + String(badgeClass(sc.mhz19_enabled,  d.co2_ok))   + "'>MH-Z19</span>";
    chunk += "<span class='sensor-badge " + String(badgeClass(sc.bme280_enabled, d.bme_ok))   + "'>BME280</span>";
    chunk += "<span class='sensor-badge " + String(badgeClass(sc.ccs811_enabled, d.ccs_ok))   + "'>CCS811</span>";
    chunk += "<span class='sensor-badge " + String(badgeClass(sc.sfa40_enabled,  d.sfa40_ok)) + "'>SFA40</span>";
    chunk += "</span></div>";
  }
  if (d.lastReadTime > 0) {
    chunk += "<div class='data-row'><span class='data-label'>Derniere mesure</span><span class='data-value'>il y a " + String(ago) + " <span class='data-unit'>s</span></span></div>";
  }
  chunk += "<div class='data-row'><span class='data-label'>Luminosite ecran</span><span class='data-value'>"
           "<span id='bri-val'>" + String(displayGetBrightness()) + "</span>/255"
           "<input type='range' min='0' max='255' value='" + String(displayGetBrightness()) + "' style='width:80px;margin-left:8px;vertical-align:middle;'"
           " oninput=\"document.getElementById('bri-val').textContent=this.value;document.getElementById('bri-warn').style.display=this.value==='0'?'block':'none'\""
           " onchange=\"fetch('/set-brightness',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'val='+this.value})\">"
           "</span></div>"
           "<div id='bri-warn' style='display:" + String(displayGetBrightness() == 0 ? "block" : "none") + ";color:#ffcc80;font-size:0.8em;padding:4px 0 8px;'>"
           "&#9888; Ecran eteint. Pour le rallumer, revenir sur cette page et ajuster le curseur.</div>";
  chunk += "<div class='data-row'><span class='data-label'>Ecran debug au demarrage</span><span class='data-value'>"
           "<label style='cursor:pointer'><input type='checkbox' id='dbg-splash' "
           + String(displayGetDebugSplash() ? "checked" : "") +
           " onchange=\"fetch('/debug-splash',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'enabled='+(this.checked?'1':'0')})\">"
           " Actif</label></span></div>";
  chunk += "</div>";
  server.sendContent(chunk);

  // Capteurs actifs
  {
    const SensorSettings& sc = settingsGetSensors();
    chunk = "<div class='card'><h2>Capteurs actifs</h2>"
            "<p style='color:#888;font-size:0.8em;margin:0 0 8px'>Effet immediat (au prochain cycle de mesure)</p>";
    const char* sensorNames[] = { "NextPM (PM)", "MH-Z19 (CO2)", "BME280 (T/H/P)", "CCS811 (COV)", "SFA40 (HCHO)" };
    const char* sensorKeys[] = { "npm", "mhz19", "bme280", "ccs811", "sfa40" };
    bool sensorVals[] = { sc.npm_enabled, sc.mhz19_enabled, sc.bme280_enabled, sc.ccs811_enabled, sc.sfa40_enabled };
    for (int i = 0; i < 5; i++) {
      chunk += "<div class='toggle-row'><span>" + String(sensorNames[i]) + "</span>";
      chunk += "<label class='switch'><input type='checkbox'";
      if (sensorVals[i]) chunk += " checked";
      chunk += " onchange=\"fetch('/set-sensor',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key=" + String(sensorKeys[i]) + "&val='+(this.checked?'1':'0')})\">";
      chunk += "<span class='slider'></span></label></div>";
    }
    chunk += "</div>";
    server.sendContent(chunk);
  }

  // Ecrans affichés
  {
    const ScreenSettings& ss = settingsGetScreens();
    const SensorSettings& sc = settingsGetSensors();
    chunk = "<div class='card'><h2>Ecrans matrice</h2>";

    // Polluants. Each pollutant screen is locked when its parent sensor is
    // disabled — we keep the user's saved preference (it'll come back when
    // they re-enable the sensor) but the toggle is greyed out and not
    // clickable to avoid the "screen toggle is on but nothing shows" trap.
    chunk += "<p style='color:#4fc3f7;font-size:0.85em;margin:0 0 6px;font-weight:bold'>Polluants</p>";
    const char* pollNames[]  = { "PM1", "PM2.5", "PM10", "CO2", "Temperature", "Humidite", "COV (TVOC)", "Formaldehyde" };
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
      if (locked) chunk += "<span class='toggle-hint'>(capteur off)</span>";
      chunk += "</span>";
      chunk += "<label class='switch'><input type='checkbox'";
      if (pollVals[i]) chunk += " checked";
      if (locked) chunk += " disabled";
      chunk += " onchange=\"fetch('/set-screen',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key=" + String(pollKeys[i]) + "&val='+(this.checked?'1':'0')})\">";
      chunk += "<span class='slider'></span></label></div>";
    }

    // Logos
    chunk += "<p style='color:#4fc3f7;font-size:0.85em;margin:12px 0 6px;font-weight:bold'>Logos</p>";
    const char* logoNames[] = { "ModuleAir", "AirCarto", "AtmoSud" };
    const char* logoKeys[] = { "logo_ma", "logo_ac", "logo_as" };
    bool logoVals[] = { ss.logo_moduleair, ss.logo_aircarto, ss.logo_atmosud };
    for (int i = 0; i < 3; i++) {
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
    chunk = "<div class='card'><h2>Seuils d'alerte</h2>";
    chunk += "<p style='color:#4fc3f7;font-size:0.85em;margin:0 0 6px;font-weight:bold'>CO2 (ppm)</p>";
    chunk += "<div class='data-row'><span class='data-label' style='color:#a5d6a7'>Bon &lt;</span>";
    chunk += "<span class='data-value'><input type='number' id='co2-good' value='" + String(co2th.good) + "' style='width:60px;background:#0f3460;color:#fff;border:1px solid #333;border-radius:4px;padding:4px;text-align:center;'></span></div>";
    chunk += "<div class='data-row'><span class='data-label' style='color:#ef9a9a'>Mauvais &ge;</span>";
    chunk += "<span class='data-value'><input type='number' id='co2-bad' value='" + String(co2th.bad) + "' style='width:60px;background:#0f3460;color:#fff;border:1px solid #333;border-radius:4px;padding:4px;text-align:center;'></span></div>";
    chunk += "<div style='color:#888;font-size:0.8em;padding:4px 0;'>Entre les deux = orange (Aerer SVP)</div>";
    chunk += "<div style='display:flex;gap:8px;margin-top:4px;'>"
             "<button style='width:auto;padding:8px 20px;' onclick=\"fetch('/set-thresholds-co2',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'good='+document.getElementById('co2-good').value+'&bad='+document.getElementById('co2-bad').value}).then(()=>{this.textContent='OK !';setTimeout(()=>this.textContent='Appliquer',1500)})\">Appliquer</button>"
             "<button style='width:auto;padding:8px 12px;background:#333;color:#aaa;' onclick=\"document.getElementById('co2-good').value='800';document.getElementById('co2-bad').value='1500';fetch('/set-thresholds-co2',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'good=800&bad=1500'}).then(()=>{this.textContent='Restaure !';setTimeout(()=>this.textContent='Par defaut',1500)})\">Par defaut</button>"
             "</div>";
    chunk += "</div>";
    server.sendContent(chunk);
  }

  // Logs — tail incremental avec curseur seq, smart scroll, pause/clear
  server.sendContent(
    "<div class='card wide'><h2 style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px'>"
    "<span>Logs en temps reel</span>"
    "<span style='font-size:0.75em;font-weight:normal;color:#888'>"
    "<span id='log-status' style='margin-right:10px'>&#9679; live</span>"
    "<button id='log-pause' style='width:auto;padding:4px 10px;font-size:0.9em' onclick='toggleLogPause()'>Pause</button> "
    "<button style='width:auto;padding:4px 10px;font-size:0.9em;background:#333;color:#aaa' onclick='clearLogs()'>Vider</button>"
    "</span></h2>"
    "<pre id='log-box' style='background:#0a0a1a;padding:10px;border-radius:8px;"
    "height:50vh;min-height:300px;overflow-y:auto;font-size:0.8em;color:#aaa;"
    "white-space:pre-wrap;word-break:break-all;margin:0'>Chargement...</pre>"
    "<script>"
    "var logSeq=0,logPaused=false,logFirst=true;"
    "var logBox=document.getElementById('log-box');"
    "var logStatus=document.getElementById('log-status');"
    "function clearLogs(){logBox.textContent='';}"
    "function toggleLogPause(){"
    "logPaused=!logPaused;"
    "document.getElementById('log-pause').textContent=logPaused?'Reprendre':'Pause';"
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
    "if(logFirst){logBox.textContent=t||'(aucun log)';logFirst=false;logBox.scrollTop=logBox.scrollHeight;return;}"
    "if(!t)return;"
    "var atBottom=logBox.scrollHeight-logBox.scrollTop-logBox.clientHeight<60;"
    "logBox.textContent+=t;"
    "if(atBottom)logBox.scrollTop=logBox.scrollHeight;"
    "}).catch(function(){});"
    "}"
    "logStatus.style.color='#a5d6a7';"
    "tailLogs();setInterval(tailLogs,1000);"
    "</script></div>");

  // Mise à jour OTA
  server.sendContent(
    "<div class='card wide'><h2>Mise a jour</h2>"
    "<div id='ota-status'></div>"
    "<button class='update' onclick='checkUpdate()'>Verifier les mises a jour</button>"
    "<button id='ota-btn' class='update'></button>"
    "</div>");

  // Boutons oublier WiFi + redémarrer
  server.sendContent(
    "<div class='card wide'>"
    "<form action='/reset' method='POST'>"
    "<button type='submit' class='danger'>Oublier le WiFi</button>"
    "</form>"
    "<form action='/reboot' method='POST'>"
    "<button type='submit' class='danger'>Redemarrer le capteur</button>"
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
  String apStatus = "<div class='card'><h2>Statut</h2>"
    "<div class='status ap'>Mode point d'acces<br>"
    "SSID: " + apSSID + "</div></div>";
  server.sendContent(apStatus);

  // Réseaux WiFi
  String chunk = "<div class='card'><h2>Reseaux WiFi</h2>";
  chunk += "<div id='wifi-list'>";
  if (n <= 0) {
    chunk += "<div class='scan-info'>Aucun reseau trouve</div>";
  } else {
    chunk += "<div class='scan-info'>" + String(n) + " reseau(x) trouve(s)</div>";
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
      chunk += "<div class='pw-wrap'><input type='password' name='password' placeholder='Mot de passe'><button type='button' class='pw-toggle' onclick='togglePw(this)'>&#128065;</button></div>";
    } else {
      chunk += "<input type='hidden' name='password' value=''>";
    }
    chunk += "<button type='submit'>Connecter</button>";
    chunk += "</form></div>";

    server.sendContent(chunk);
  }
  server.sendContent("</div>");  // fin wifi-list
  server.sendContent("<button id='scan-btn' onclick='scanWifi()'>Actualiser les reseaux</button>");
  server.sendContent("</div>");  // fin card

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

static void handleRoot() {
  Logger.printf("[Web] Client %s requested /\n", server.client().remoteIP().toString().c_str());
  if (wifiConnected) {
    handleRootConnected();
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
      "<div class='card'><div class='status ap'>SSID requis</div>"
      "<br><a href='/'><button>Retour</button></a></div>");
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
    "<div class='card'><div class='status ok'>Configuration enregistree !<br><br>"
    "SSID: <strong>" + ssid + "</strong><br><br>"
    "Le capteur va redemarrer et tenter de se connecter.</div></div>"
    "<div class='card'><h2>Indicateurs lumineux</h2>"
    "<div class='data-row'><span class='data-label' style='color:#4fc3f7'>Clignotements bleus</span>"
    "<span class='data-value'>Connexion WiFi reussie</span></div>"
    "<div class='data-row'><span class='data-label' style='color:#ff7043'>Respiration orange</span>"
    "<span class='data-value'>Echec, retour en mode AP</span></div>"
    "</div>");
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
    "<div class='msg'><h2>WiFi oublie !</h2>"
    "<p>Redemarrage en mode point d'acces...</p>"
    "<p>Connectez-vous au reseau <strong>" + apSSID + "</strong></p></div>"
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
    "<div class='msg'><h2>Redemarrage en cours...</h2>"
    "<p>Retour automatique dans 10 secondes</p></div>"
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
  settingsSetScreenEnabled(key.c_str(), val);
  Logger.printf("[Web] Screen %s = %s\n", key.c_str(), val ? "on" : "off");
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

  // Streaming chunked : evite de construire une grosse String en heap
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/plain", "");
  loggerForEachLineSince(since, [](uint32_t seq, const char* line) {
    server.sendContent(line);
    server.sendContent("\n");
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

static void handleCheckUpdate() {
  Logger.println("[OTA] Checking for update...");
  WiFiClientSecure secClient;
  secClient.setInsecure();  // Skip certificate validation (simplifies deployment)
  HTTPClient http;
  String versionUrl = String(OTA_UPDATE_URL) + "/version.txt?sensor=" + deviceId + "&current_version=" + FIRMWARE_VERSION;
  http.begin(secClient, versionUrl);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode != 200) {
    Logger.printf("[OTA] Version check failed (HTTP %d)\n", httpCode);
    http.end();
    server.send(200, "application/json", "{\"error\":\"Serveur inaccessible (HTTP " + String(httpCode) + ")\"}");
    return;
  }

  String remoteVersion = http.getString();
  remoteVersion.trim();
  http.end();

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

// =============================================
// Init & Loop
// =============================================

void wifiManagerInit() {
  preferences.begin("wifi", true);
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");
  preferences.end();

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
      wifiConnected = true;
      apMode = false;
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
      Logger.println("[WiFi] Falling back to AP mode...");
    }
  } else {
    Logger.println("[WiFi] No saved credentials found");
  }

  if (!wifiConnected) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID.c_str(), AP_PASSWORD);
    apMode = true;
    Logger.printf("[WiFi] AP started\n");
    Logger.printf("[WiFi] SSID:     %s\n", apSSID.c_str());
    Logger.printf("[WiFi] Password: %s\n", AP_PASSWORD);
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
    if (apMode) {
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
  if (apMode) {
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

void wifiManagerLoop() {
  if (apMode) {
    dnsServer.processNextRequest();
    bleImprovLoop();
  }
  server.handleClient();

  // Vérifier si le scan async est terminé
  if (scanInProgress) {
    int result = WiFi.scanComplete();
    if (result != WIFI_SCAN_RUNNING) {
      scanCount = (result == WIFI_SCAN_FAILED) ? 0 : result;
      scanInProgress = false;
      Logger.printf("[WiFi] Scan complete: %d networks found\n", scanCount);
    }
  }

  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  if (now - lastCheck < 10000) return;
  lastCheck = now;

  if (apMode) {
    int clients = WiFi.softAPgetStationNum();
    if (clients != lastAPClients) {
      Logger.printf("[WiFi] AP clients: %d -> %d\n", lastAPClients, clients);
      lastAPClients = clients;
    }
  } else if (wifiConnected) {
    if (WiFi.status() != WL_CONNECTED) {
      Logger.println("[WiFi] Connection lost! Reconnecting...");
      WiFi.reconnect();
    }
  }
}

bool wifiIsConnected() {
  return wifiConnected && WiFi.status() == WL_CONNECTED;
}

bool wifiIsApMode() {
  return apMode;
}
