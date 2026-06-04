#include "i18n.h"
#include <Preferences.h>
#include "logger.h"

// ── French table ────────────────────────────────────────────────────────────
// Matrix-drawn fields embed raw font bytes for accents (0x82=é, 0x83=ç-glyph).
static const I18nStrings STR_FR = {
  // pollutant levels
  /* lvl_good        */ "BON",
  /* lvl_medium      */ "MOYEN",
  /* lvl_degraded    */ "DEGRADE",
  /* lvl_bad         */ "MAUVAIS",
  /* co2_ventilate   */ "AERER SVP",
  /* temp_cold       */ "FROID",
  /* temp_ok         */ "OK",
  /* temp_hot        */ "CHAUD",
  /* humi_dry        */ "SEC",
  /* humi_ideal      */ "IDEAL",
  /* humi_wet        */ "HUMIDE",
  /* scr_sensor      */ "Capteur",
  /* scr_error       */ "Erreur",
  /* scr_config      */ "Config",
  /* scr_wifi        */ "WiFi",
  /* scr_connecting  */ "Connexion",
  /* scr_connected   */ "Connect\x82",      // Connecté
  /* scr_disconnected*/ "Deconnect\x82",    // Déconnecté (matrix)
  /* ble_connected   */ "Connect\x82",
  /* ble_creds_l1    */ "Identifiant",
  /* ble_creds_l2    */ "re\x83us",         // reçus
  /* ble_connecting  */ "Connexion",
  /* ble_wifi_ok     */ "WiFi OK!",
  /* ble_fail        */ "Echec!",
  /* ble_config_ok   */ "Config OK",
  /* ble_reboot      */ "Reboot...",
  /* ota_upd_l1      */ "Mise a",
  /* ota_upd_l2      */ "jour",
  /* ota_done_l1     */ "Mise a jour",
  /* ota_done_l2     */ "reussi !",
  /* ota_fail_l1     */ "Erreur",
  /* ota_fail_l2     */ "MAJ",

  // web common
  /* web_refresh     */ "Rafraichir",
  /* web_waiting     */ "En attente de donnees...",
  /* web_back        */ "Retour",

  // web alerts
  /* alert_title     */ "Alertes capteurs",
  /* npm_mute        */ "Communication impossible (capteur muet)",
  /* npm_aberrant    */ "Valeur aberrante ou lecture echouee",
  /* npm_laser       */ "laser HS",
  /* npm_mem         */ "memoire KO",
  /* npm_fan         */ "ventilateur HS",
  /* npm_th          */ "T/H interne KO",
  /* npm_humid       */ "humidite excessive",
  /* npm_notready    */ "pas pret",
  /* npm_degraded    */ "mode degrade",
  /* npm_sleep       */ "en veille",
  /* co2_read_err    */ "Erreur de lecture (voir logs)",
  /* sensor_not_found*/ "Capteur introuvable",

  // web connection
  /* card_connection */ "Connexion",
  /* web_network     */ "Reseau",
  /* web_signal      */ "Signal",
  /* sig_excellent   */ "Excellent",
  /* sig_good        */ "Bon",
  /* sig_medium      */ "Moyen",
  /* sig_weak        */ "Faible",
  /* web_uptime      */ "Uptime",
  /* uptime_day      */ "j",

  // web measurement cards
  /* card_pm         */ "Particules fines (NextPM)",
  /* card_co2        */ "CO2 (MH-Z19)",
  /* card_cov        */ "COV (CCS811)",
  /* card_hcho       */ "Formaldehyde (SFA40)",
  /* card_env        */ "Environnement (BME280)",
  /* web_temperature */ "Temperature",
  /* web_humidity    */ "Humidite",
  /* web_pressure    */ "Pression",

  // web system card
  /* card_system     */ "Systeme",
  /* web_free_ram    */ "RAM libre",
  /* web_sensors     */ "Capteurs",
  /* web_last_measure*/ "Derniere mesure",
  /* web_ago_pre     */ "il y a ",
  /* web_ago_post    */ " s",
  /* web_brightness  */ "Luminosite ecran",
  /* web_screen_off  */ "&#9888; Ecran eteint. Pour le rallumer, revenir sur cette page et ajuster le curseur.",
  /* web_debug_splash*/ "Ecran debug au demarrage",
  /* web_active      */ "Actif",

  // web active-sensors card
  /* card_active_sensors */ "Capteurs actifs",
  /* web_immediate       */ "Effet immediat (au prochain cycle de mesure)",
  /* sensor_cov_name     */ "CCS811 (COV)",

  // web matrix-screens card
  /* card_matrix_screens */ "Ecrans matrice",
  /* web_pollutants      */ "Polluants",
  /* web_sensor_off      */ "(capteur off)",
  /* web_logos           */ "Logos",
  /* poll_humi_name      */ "Humidite",
  /* poll_cov_name       */ "COV (TVOC)",

  // web thresholds card
  /* card_thresholds */ "Seuils d'alerte",
  /* web_th_good     */ "Bon &lt;",
  /* web_th_bad      */ "Mauvais &ge;",
  /* web_th_between  */ "Entre les deux = orange (Aerer SVP)",
  /* web_apply       */ "Appliquer",
  /* web_ok_excl     */ "OK !",
  /* web_default     */ "Par defaut",
  /* web_restored    */ "Restaure !",

  // web logs card
  /* card_logs       */ "Logs en temps reel",
  /* web_pause       */ "Pause",
  /* web_resume      */ "Reprendre",
  /* web_clear       */ "Vider",
  /* web_loading     */ "Chargement...",
  /* web_no_logs     */ "(aucun log)",

  // web OTA card
  /* card_update     */ "Mise a jour",
  /* web_check_update*/ "Verifier les mises a jour",

  // web danger buttons
  /* web_forget_wifi */ "Oublier le WiFi",
  /* web_reboot      */ "Redemarrer le capteur",

  // web language
  /* card_language   */ "Langue",

  // web AP / portal
  /* ap_status               */ "Statut",
  /* ap_mode                 */ "Mode point d'acces",
  /* ap_networks             */ "Reseaux WiFi",
  /* ap_no_network           */ "Aucun reseau trouve",
  /* ap_networks_found_suffix*/ " reseau(x) trouve(s)",
  /* ap_password             */ "Mot de passe",
  /* ap_connect              */ "Connecter",
  /* ap_refresh              */ "Actualiser les reseaux",
  /* ap_ssid_required        */ "SSID requis",
  /* ap_saved_title          */ "Configuration enregistree !",
  /* ap_saved_body           */ "Le capteur va redemarrer et tenter de se connecter.",
  /* ap_forgotten            */ "WiFi oublie !",
  /* ap_restart_ap           */ "Redemarrage en mode point d'acces...",
  /* ap_connect_to           */ "Connectez-vous au reseau ",
  /* reboot_title            */ "Redemarrage en cours...",
  /* reboot_body             */ "Retour automatique dans 10 secondes",

  // JS-side
  /* js_searching        */ "Recherche...",
  /* js_networks_found   */ " reseau(x) trouve(s)",
  /* js_no_network       */ "Aucun reseau trouve",
  /* js_password         */ "Mot de passe",
  /* js_connect          */ "Connecter",
  /* js_refresh_networks */ "Actualiser les reseaux",
  /* js_checking         */ "Verification en cours...",
  /* js_new_version      */ "Nouvelle version disponible : v",
  /* js_update_to        */ "Mettre a jour vers v",
  /* js_failed           */ "Echec : ",
  /* js_up_to_date       */ "Firmware a jour (v",
  /* js_conn_error       */ "Erreur de connexion",
  /* js_confirm_update   */ "Lancer la mise a jour ? Le capteur va redemarrer.",
  /* js_downloading      */ "Telechargement et installation en cours...<br>Ne pas eteindre le capteur !",
  /* js_update_ok        */ "Mise a jour reussie ! Redemarrage...",
  /* js_update_running   */ "Mise a jour en cours, le capteur redemarre...",
};

// ── English table ───────────────────────────────────────────────────────────
static const I18nStrings STR_EN = {
  // pollutant levels
  /* lvl_good        */ "GOOD",
  /* lvl_medium      */ "MEDIUM",
  /* lvl_degraded    */ "POOR",
  /* lvl_bad         */ "BAD",
  /* co2_ventilate   */ "VENTILATE",
  /* temp_cold       */ "COLD",
  /* temp_ok         */ "OK",
  /* temp_hot        */ "HOT",
  /* humi_dry        */ "DRY",
  /* humi_ideal      */ "IDEAL",
  /* humi_wet        */ "HUMID",
  /* scr_sensor      */ "Sensor",
  /* scr_error       */ "Error",
  /* scr_config      */ "Setup",
  /* scr_wifi        */ "WiFi",
  /* scr_connecting  */ "Connecting",
  /* scr_connected   */ "Connected",
  /* scr_disconnected*/ "Disconnect",
  /* ble_connected   */ "Connected",
  /* ble_creds_l1    */ "WiFi creds",
  /* ble_creds_l2    */ "saved",
  /* ble_connecting  */ "Connecting",
  /* ble_wifi_ok     */ "WiFi OK!",
  /* ble_fail        */ "Failed!",
  /* ble_config_ok   */ "Setup OK",
  /* ble_reboot      */ "Reboot...",
  /* ota_upd_l1      */ "Firmware",
  /* ota_upd_l2      */ "update",
  /* ota_done_l1     */ "Update",
  /* ota_done_l2     */ "done!",
  /* ota_fail_l1     */ "Update",
  /* ota_fail_l2     */ "failed",

  // web common
  /* web_refresh     */ "Refresh",
  /* web_waiting     */ "Waiting for data...",
  /* web_back        */ "Back",

  // web alerts
  /* alert_title     */ "Sensor alerts",
  /* npm_mute        */ "Communication failed (sensor mute)",
  /* npm_aberrant    */ "Invalid value or read failed",
  /* npm_laser       */ "laser dead",
  /* npm_mem         */ "memory fault",
  /* npm_fan         */ "fan dead",
  /* npm_th          */ "internal T/H fault",
  /* npm_humid       */ "excessive humidity",
  /* npm_notready    */ "not ready",
  /* npm_degraded    */ "degraded mode",
  /* npm_sleep       */ "sleeping",
  /* co2_read_err    */ "Read error (see logs)",
  /* sensor_not_found*/ "Sensor not found",

  // web connection
  /* card_connection */ "Connection",
  /* web_network     */ "Network",
  /* web_signal      */ "Signal",
  /* sig_excellent   */ "Excellent",
  /* sig_good        */ "Good",
  /* sig_medium      */ "Medium",
  /* sig_weak        */ "Weak",
  /* web_uptime      */ "Uptime",
  /* uptime_day      */ "d",

  // web measurement cards
  /* card_pm         */ "Particulate matter (NextPM)",
  /* card_co2        */ "CO2 (MH-Z19)",
  /* card_cov        */ "VOC (CCS811)",
  /* card_hcho       */ "Formaldehyde (SFA40)",
  /* card_env        */ "Environment (BME280)",
  /* web_temperature */ "Temperature",
  /* web_humidity    */ "Humidity",
  /* web_pressure    */ "Pressure",

  // web system card
  /* card_system     */ "System",
  /* web_free_ram    */ "Free RAM",
  /* web_sensors     */ "Sensors",
  /* web_last_measure*/ "Last reading",
  /* web_ago_pre     */ "",
  /* web_ago_post    */ " s ago",
  /* web_brightness  */ "Screen brightness",
  /* web_screen_off  */ "&#9888; Screen off. To turn it back on, return to this page and move the slider.",
  /* web_debug_splash*/ "Debug screen at startup",
  /* web_active      */ "Active",

  // web active-sensors card
  /* card_active_sensors */ "Active sensors",
  /* web_immediate       */ "Immediate effect (next measurement cycle)",
  /* sensor_cov_name     */ "CCS811 (VOC)",

  // web matrix-screens card
  /* card_matrix_screens */ "Matrix screens",
  /* web_pollutants      */ "Pollutants",
  /* web_sensor_off      */ "(sensor off)",
  /* web_logos           */ "Logos",
  /* poll_humi_name      */ "Humidity",
  /* poll_cov_name       */ "VOC (TVOC)",

  // web thresholds card
  /* card_thresholds */ "Alert thresholds",
  /* web_th_good     */ "Good &lt;",
  /* web_th_bad      */ "Bad &ge;",
  /* web_th_between  */ "In between = orange (please ventilate)",
  /* web_apply       */ "Apply",
  /* web_ok_excl     */ "OK!",
  /* web_default     */ "Default",
  /* web_restored    */ "Restored!",

  // web logs card
  /* card_logs       */ "Live logs",
  /* web_pause       */ "Pause",
  /* web_resume      */ "Resume",
  /* web_clear       */ "Clear",
  /* web_loading     */ "Loading...",
  /* web_no_logs     */ "(no logs)",

  // web OTA card
  /* card_update     */ "Firmware update",
  /* web_check_update*/ "Check for updates",

  // web danger buttons
  /* web_forget_wifi */ "Forget WiFi",
  /* web_reboot      */ "Restart sensor",

  // web language
  /* card_language   */ "Language",

  // web AP / portal
  /* ap_status               */ "Status",
  /* ap_mode                 */ "Access point mode",
  /* ap_networks             */ "WiFi networks",
  /* ap_no_network           */ "No network found",
  /* ap_networks_found_suffix*/ " network(s) found",
  /* ap_password             */ "Password",
  /* ap_connect              */ "Connect",
  /* ap_refresh              */ "Refresh networks",
  /* ap_ssid_required        */ "SSID required",
  /* ap_saved_title          */ "Configuration saved!",
  /* ap_saved_body           */ "The sensor will restart and try to connect.",
  /* ap_forgotten            */ "WiFi forgotten!",
  /* ap_restart_ap           */ "Restarting in access point mode...",
  /* ap_connect_to           */ "Connect to the network ",
  /* reboot_title            */ "Restarting...",
  /* reboot_body             */ "Auto-return in 10 seconds",

  // JS-side
  /* js_searching        */ "Searching...",
  /* js_networks_found   */ " network(s) found",
  /* js_no_network       */ "No network found",
  /* js_password         */ "Password",
  /* js_connect          */ "Connect",
  /* js_refresh_networks */ "Refresh networks",
  /* js_checking         */ "Checking...",
  /* js_new_version      */ "New version available: v",
  /* js_update_to        */ "Update to v",
  /* js_failed           */ "Failed: ",
  /* js_up_to_date       */ "Firmware up to date (v",
  /* js_conn_error       */ "Connection error",
  /* js_confirm_update   */ "Start the update? The sensor will restart.",
  /* js_downloading      */ "Downloading and installing...<br>Do not power off the sensor!",
  /* js_update_ok        */ "Update successful! Restarting...",
  /* js_update_running   */ "Update in progress, the sensor is restarting...",
};

// ── State ───────────────────────────────────────────────────────────────────

#ifdef DEFAULT_LANG_EN
  static const Lang DEFAULT_LANG = LANG_EN;
#else
  static const Lang DEFAULT_LANG = LANG_FR;
#endif

static Lang currentLang = DEFAULT_LANG;
static const I18nStrings* active = &STR_FR;

static const I18nStrings* tableFor(Lang l) {
  return (l == LANG_EN) ? &STR_EN : &STR_FR;
}

void i18nInit() {
  Preferences prefs;
  prefs.begin("i18n", true);
  // First boot: NVS empty -> use the compile-time default. Once the user picks
  // a language in the web UI it's stored here and wins on every later boot,
  // surviving OTA (NVS is never touched by a firmware update).
  uint8_t stored = prefs.getUChar("lang", (uint8_t)DEFAULT_LANG);
  prefs.end();

  currentLang = (stored == LANG_EN) ? LANG_EN : LANG_FR;
  active = tableFor(currentLang);
  Logger.printf("[i18n] Language: %s (default %s)\n",
                i18nLangCode(currentLang), i18nLangCode(DEFAULT_LANG));
}

Lang i18nGetLang() { return currentLang; }

void i18nSetLang(Lang lang) {
  currentLang = (lang == LANG_EN) ? LANG_EN : LANG_FR;
  active = tableFor(currentLang);
  Preferences prefs;
  prefs.begin("i18n", false);
  prefs.putUChar("lang", (uint8_t)currentLang);
  prefs.end();
  Logger.printf("[i18n] Language set to %s\n", i18nLangCode(currentLang));
}

const char* i18nLangCode(Lang l) { return (l == LANG_EN) ? "EN" : "FR"; }

const I18nStrings& TR() { return *active; }

// Build a small JS dictionary so the static client JS can stay language-free
// and just reference L.xxx. Values are wrapped in single quotes; none of the
// strings contain a single quote or backslash, so no escaping is needed.
String jsLangDict() {
  const I18nStrings& t = TR();
  String s = F("<script>var L={");
  s += "searching:'" + String(t.js_searching) + "',";
  s += "networksFound:'" + String(t.js_networks_found) + "',";
  s += "noNetwork:'" + String(t.js_no_network) + "',";
  s += "password:'" + String(t.js_password) + "',";
  s += "connect:'" + String(t.js_connect) + "',";
  s += "refreshNetworks:'" + String(t.js_refresh_networks) + "',";
  s += "checking:'" + String(t.js_checking) + "',";
  s += "newVersion:'" + String(t.js_new_version) + "',";
  s += "updateTo:'" + String(t.js_update_to) + "',";
  s += "failed:'" + String(t.js_failed) + "',";
  s += "upToDate:'" + String(t.js_up_to_date) + "',";
  s += "connError:'" + String(t.js_conn_error) + "',";
  s += "confirmUpdate:'" + String(t.js_confirm_update) + "',";
  s += "downloading:'" + String(t.js_downloading) + "',";
  s += "updateOk:'" + String(t.js_update_ok) + "',";
  s += "updateRunning:'" + String(t.js_update_running) + "'";
  s += F("};</script>");
  return s;
}
