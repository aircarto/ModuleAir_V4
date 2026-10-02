#include <Arduino.h>
#include <esp_mac.h>
#include "config.h"
#include "logger.h"

String deviceId;
String apSSID;
String bleName;

void configInit() {
  // Générer le device ID depuis le MAC
  uint64_t mac = ESP.getEfuseMac();
  deviceId = String((uint16_t)(mac >> 32), HEX);
  deviceId += String((uint32_t)mac, HEX);
  deviceId.toUpperCase();

  // SSID AP unique avec les 6 derniers caractères du MAC
  apSSID = "ModuleAir-" + deviceId.substring(deviceId.length() - 6);

  // Nom BLE : le device ID COMPLET, contrairement au SSID de l'AP.
  // C'est la seule information que l'appli mobile obtient d'un capteur en mode
  // configuration : tant qu'elle n'est pas connectée elle ne voit que le nom
  // annoncé, et le device ID n'arrive sinon qu'APRÈS le provisioning (résultat
  // RPC Improv). Comme le device ID est aussi le token côté serveur, le donner
  // ici permet à l'appli d'afficher le vrai nom du capteur ("moduleair-m103")
  // dès la liste des capteurs détectés, au lieu du suffixe brut de la MAC.
  // 22 caractères : trop long pour la trame d'annonce (31 octets, dont 18 pris
  // par l'UUID 128 bits du service Improv), NimBLE place donc le nom complet
  // dans la réponse de scan — 24 octets sur 31, et setScanResponse(true) est
  // déjà actif côté bleImprovInit(). La limite GAP est de 31 caractères.
  bleName = "ModuleAir-" + deviceId;

  Logger.printf("[Config] Device ID: %s\n", deviceId.c_str());
  Logger.printf("[Config] AP SSID:   %s\n", apSSID.c_str());
  Logger.printf("[Config] BLE name:  %s\n", bleName.c_str());
}
