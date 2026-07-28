#ifndef I18N_H
#define I18N_H

#include <Arduino.h>

// ── Internationalisation (runtime, FR/EN) ───────────────────────────────────
//
// One firmware contains BOTH languages. The active language is a runtime
// setting stored in NVS (namespace "i18n", key "lang"), exactly like the
// sensor/screen/threshold toggles — so it survives an OTA update for free
// (an OTA only rewrites the app partition, never NVS) and can be flipped from
// the web UI with immediate effect.
//
// The FIRST-BOOT default is chosen at build time: define DEFAULT_LANG_EN (see
// the *_en PlatformIO envs) to make a freshly-flashed board boot in English.
// Without that flag the default is French. The default is NOT permanent — once
// the user (or NVS) holds a value it wins on every later boot.
//
// Strings live in two static `I18nStrings` tables (FR / EN). Access the active
// one through TR() :  display.print(TR().lvl_good);  /  "<h2>" + TR().card_co2.
//
// Matrix-font note: the 64x32 LED matrix uses a modified glcdfont where a few
// high bytes map to accented glyphs (0x82 = é, 0x83 = the ç used in "reçus",
// etc.). French strings drawn on the matrix embed those raw bytes (e.g.
// "Connect\x82"); the English strings are plain ASCII.

enum Lang : uint8_t {
  LANG_FR = 0,
  LANG_EN = 1,
};

struct I18nStrings {
  // ── Display: pollutant level words (shared PM / VOC / HCHO) ──
  const char* lvl_good;
  const char* lvl_medium;
  const char* lvl_degraded;
  const char* lvl_bad;
  // ── Display: CO2 status row ──
  const char* co2_ventilate;   // mid level: "AERER SVP" / "VENTILATE"
  // ── Display: temperature status ──
  const char* temp_cold;
  const char* temp_ok;
  const char* temp_hot;
  // ── Display: humidity status ──
  const char* humi_dry;
  const char* humi_ideal;
  const char* humi_wet;
  // ── Display: error screen ──
  const char* scr_sensor;      // top label "Capteur" / "Sensor"
  const char* scr_error;       // bottom "Erreur" / "Error"
  // ── Display: config-wifi + AP splash ──
  const char* scr_config;      // "Config"
  const char* scr_wifi;        // "WiFi"
  // ── Display: wifi status ──
  const char* scr_connecting;  // "Connexion" / "Connecting"
  const char* scr_connected;   // "Connect\x82" (Connecté) / "Connected"
  const char* scr_disconnected;// "Deconnect\x82" / "Disconnect"
  // ── Display: wifi connect-failure motif (boot STA fallback) ──
  const char* scr_fail_auth;   // "Mdp refuse" / "Bad pass"
  const char* scr_fail_noap;   // "Absent"     / "No AP"
  const char* scr_fail_other;  // "Echec"      / "Failed"
  // ── Display: BLE provisioning ──
  const char* ble_connected;   // "Connect\x82" / "Connected"
  const char* ble_creds_l1;    // "Identifiant" / "WiFi creds"
  const char* ble_creds_l2;    // "re\x83us" (reçus) / "saved"
  const char* ble_connecting;  // "Connexion" / "Connecting"
  const char* ble_wifi_ok;     // "WiFi OK!"
  const char* ble_fail;        // "Echec!" / "Failed!"
  const char* ble_config_ok;   // "Config OK"
  const char* ble_reboot;      // "Reboot..."
  // ── Display: OTA screens ──
  const char* ota_upd_l1;      // "Mise a" / "Firmware"
  const char* ota_upd_l2;      // "jour"   / "update"
  const char* ota_done_l1;     // "Mise a jour" / "Update"
  const char* ota_done_l2;     // "reussi !"     / "done!"
  const char* ota_fail_l1;     // "Erreur" / "Update"
  const char* ota_fail_l2;     // "MAJ"    / "failed"

  // ── Web: common chrome ──
  const char* web_refresh;     // "Rafraichir" / "Refresh"
  const char* web_waiting;     // "En attente de donnees..." / "Waiting for data..."
  const char* web_back;        // "Retour" / "Back"

  // ── Web: sensor-alert banner ──
  const char* alert_title;     // "Alertes capteurs" / "Sensor alerts"
  const char* npm_mute;
  const char* npm_aberrant;
  const char* npm_laser;
  const char* npm_mem;
  const char* npm_fan;
  const char* npm_th;
  const char* npm_humid;
  const char* npm_notready;
  const char* npm_degraded;
  const char* npm_sleep;
  const char* co2_read_err;    // "Erreur de lecture (voir logs)" / "Read error (see logs)"
  const char* sensor_not_found;// "Capteur introuvable" / "Sensor not found"

  // ── Web: connection card ──
  const char* card_connection; // "Connexion" / "Connection"
  const char* web_network;     // "Reseau" / "Network"
  const char* web_signal;      // "Signal"
  const char* sig_excellent;
  const char* sig_good;
  const char* sig_medium;
  const char* sig_weak;
  const char* web_uptime;      // "Uptime"
  const char* uptime_day;      // day unit: "j" / "d"

  // ── Web: measurement cards ──
  const char* card_pm;         // "Particules fines (NextPM)" / "Particulate matter (NextPM)"
  const char* card_co2;        // "CO2 (MH-Z19)"
  const char* card_cov;        // "COV (CCS811)" / "VOC (CCS811)"
  const char* card_hcho;       // "Formaldehyde (SFA40)"
  const char* card_env;        // "Environnement (BME280)" / "Environment (BME280)"
  const char* web_temperature; // "Temperature"
  const char* web_humidity;    // "Humidite" / "Humidity"
  const char* web_pressure;    // "Pression" / "Pressure"

  // ── Web: system card ──
  const char* card_system;     // "Systeme" / "System"
  const char* web_free_ram;    // "RAM libre" / "Free RAM"
  const char* web_sensors;     // "Capteurs" / "Sensors"
  const char* web_last_measure;// "Derniere mesure" / "Last reading"
  const char* web_ago_pre;     // "il y a " / ""        (prefix around the value)
  const char* web_ago_post;    // " s"      / " s ago"  (suffix around the value)
  const char* web_brightness;  // "Luminosite ecran" / "Screen brightness"
  const char* web_screen_off;  // off warning
  const char* web_debug_splash;// "Ecran debug au demarrage" / "Debug screen at startup"
  const char* web_active;      // "Actif" / "Active"

  // ── Web: active-sensors card ──
  const char* card_active_sensors; // "Capteurs actifs" / "Active sensors"
  const char* web_immediate;       // "Effet immediat (au prochain cycle de mesure)"
  const char* sensor_cov_name;     // "CCS811 (COV)" / "CCS811 (VOC)"

  // ── Web: matrix-screens card ──
  const char* card_matrix_screens; // "Ecrans matrice" / "Matrix screens"
  const char* web_pollutants;      // "Polluants" / "Pollutants"
  const char* web_sensor_off;      // "(capteur off)" / "(sensor off)"
  const char* web_logos;           // "Logos"
  const char* poll_humi_name;      // "Humidite" / "Humidity"
  const char* poll_cov_name;       // "COV (TVOC)" / "VOC (TVOC)"

  // ── Web: thresholds card ──
  const char* card_thresholds; // "Seuils d'alerte" / "Alert thresholds"
  const char* web_th_good;     // "Bon <" / "Good <"
  const char* web_th_bad;      // "Mauvais &ge;" / "Bad &ge;"
  const char* web_th_between;  // "Entre les deux = orange (Aerer SVP)" / ...
  const char* web_apply;       // "Appliquer" / "Apply"
  const char* web_ok_excl;     // "OK !" / "OK!"
  const char* web_default;     // "Par defaut" / "Default"
  const char* web_restored;    // "Restaure !" / "Restored!"

  // ── Web: logs card ──
  const char* card_logs;       // "Logs en temps reel" / "Live logs"
  const char* web_pause;       // "Pause"
  const char* web_resume;      // "Reprendre" / "Resume"
  const char* web_clear;       // "Vider" / "Clear"
  const char* web_loading;     // "Chargement..." / "Loading..."
  const char* web_no_logs;     // "(aucun log)" / "(no logs)"

  // ── Web: OTA card ──
  const char* card_update;     // "Mise a jour" / "Firmware update"
  const char* web_check_update;// "Verifier les mises a jour" / "Check for updates"

  // ── Web: danger buttons ──
  const char* web_forget_wifi; // "Oublier le WiFi" / "Forget WiFi"
  const char* web_reboot;      // "Redemarrer le capteur" / "Restart sensor"

  // ── Web: language selector ──
  const char* card_language;   // "Langue" / "Language"

  // ── Web: AP / config-portal mode ──
  const char* ap_status;       // "Statut" / "Status"
  const char* ap_mode;         // "Mode point d'acces" / "Access point mode"
  const char* ap_networks;     // "Reseaux WiFi" / "WiFi networks"
  const char* ap_no_network;   // "Aucun reseau trouve" / "No network found"
  const char* ap_networks_found_suffix; // " reseau(x) trouve(s)" / " network(s) found"
  const char* ap_password;     // "Mot de passe" / "Password"
  const char* ap_connect;      // "Connecter" / "Connect"
  const char* ap_refresh;      // "Actualiser les reseaux" / "Refresh networks"
  const char* ap_ssid_required;// "SSID requis" / "SSID required"
  const char* ap_saved_title;  // "Configuration enregistree !" / "Configuration saved!"
  const char* ap_saved_body;   // "Le capteur va redemarrer..." / "The sensor will restart..."
  const char* ap_forgotten;    // "WiFi oublie !" / "WiFi forgotten!"
  const char* ap_restart_ap;   // "Redemarrage en mode point d'acces..." / "Restarting in access point mode..."
  const char* ap_connect_to;   // "Connectez-vous au reseau " / "Connect to the network "
  const char* reboot_title;    // "Redemarrage en cours..." / "Restarting..."
  const char* reboot_body;     // "Retour automatique dans 10 secondes" / "Auto-return in 10 seconds"

  // ── Web: JS-side strings (injected as a JS dictionary, see jsLangDict) ──
  const char* js_searching;        // "Recherche..." / "Searching..."
  const char* js_networks_found;   // " reseau(x) trouve(s)" / " network(s) found"
  const char* js_no_network;       // "Aucun reseau trouve" / "No network found"
  const char* js_password;         // "Mot de passe" / "Password"
  const char* js_connect;          // "Connecter" / "Connect"
  const char* js_refresh_networks; // "Actualiser les reseaux" / "Refresh networks"
  const char* js_checking;         // "Verification en cours..." / "Checking..."
  const char* js_new_version;      // "Nouvelle version disponible : v" / "New version available: v"
  const char* js_update_to;        // "Mettre a jour vers v" / "Update to v"
  const char* js_failed;           // "Echec : " / "Failed: "
  const char* js_up_to_date;       // "Firmware a jour (v" / "Firmware up to date (v"
  const char* js_conn_error;       // "Erreur de connexion" / "Connection error"
  const char* js_confirm_update;   // "Lancer la mise a jour ? ..." / "Start the update? ..."
  const char* js_downloading;      // "Telechargement ... Ne pas eteindre ..." / "Downloading ... Do not power off ..."
  const char* js_update_ok;        // "Mise a jour reussie ! Redemarrage..." / "Update successful! Restarting..."
  const char* js_update_running;   // "Mise a jour en cours, le capteur redemarre..." / "Update in progress, the sensor is restarting..."

  // ── Web: data-servers card (declaratif, non modifiable) ──
  // NB : champs ajoutes en FIN de struct (les tables i18n.cpp sont en init
  // positionnelle - ne pas inserer au milieu).
  const char* card_dataservers;    // "Envoi des donnees" / "Data upload"
  const char* ds_always;           // "Actif (toujours)" / "Active (always)"
  const char* ds_active;           // "Actif (capteur provisionne)" / "Active (provisioned sensor)"
  const char* ds_inactive;         // "Inactif (capteur non concerne)" / "Inactive (not provisioned)"
  const char* ds_note;             // explication : decide en usine, non modifiable ici

  // NB : pas de chaines "capteur CO2" ici — le modele (MH-Z19 / SenseAir) est
  // detecte automatiquement et n'apparait pas dans l'UI.
};

void i18nInit();
Lang i18nGetLang();
void i18nSetLang(Lang lang);
const char* i18nLangCode(Lang l);   // "FR" / "EN"

// Active-language string table. Cheap (returns a reference to a static table).
const I18nStrings& TR();

// Emit a `<script>var L={...};</script>` dictionary of the JS-side strings for
// the current language, so the static client JS can reference L.searching etc.
String jsLangDict();

#endif
