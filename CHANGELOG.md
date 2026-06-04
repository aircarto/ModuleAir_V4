# Changelog

Toutes les modifications notables du firmware ModuleAir V4 sont documentées ici.

Format basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/), versioning [Semantic Versioning](https://semver.org/).

## [0.3.2] - 2026-06-04

### Ajouté

- Support multilingue FR / EN (i18n) en **runtime** : tous les textes des ecrans matrice ET de l'interface web sont traduits. Un seul firmware contient les deux langues. Selecteur de langue dans l'UI web (carte "Langue / Language", disponible en mode connecte ET dans le portail AP) avec effet immediat (reload de la page ; les ecrans suivent au cycle suivant).
- Persistance de la langue en **NVS** (namespace `i18n`, cle `lang`), au meme titre que les toggles capteurs/ecrans/seuils. Consequence : la langue reste changeable a la volee et **survit a l'OTA** — la mise a jour ne reecrit que la partition applicative, jamais la NVS. Un capteur configure en anglais reste donc en anglais apres chaque MAJ, sans aucune machinerie de sauvegarde/restauration.
- 4 environnements PlatformIO selectionnables au flash : `moduleair` (classique FR), `moduleair_en` (classique EN), `atmosud` (AtmoSud FR), `atmosud_en` (AtmoSud EN). Le flag de build `-DDEFAULT_LANG_EN` ne fixe que la langue de **premier boot** (quand la NVS est vierge).
- Ecran matrice "mesure air interieur" en anglais : ce splash est une image (texte grave dans les pixels, pas du texte rendu par police), donc une version anglaise du bitmap `interieur_no_connection_en` (importee de ModuleAir-Next-Gen) est selectionnee quand la langue active est EN. Le bitmap est `const` (reside en flash, pas en DRAM) pour ne pas alourdir la RAM.

### Modifié

- Architecture i18n : nouveau module `src/i18n.{h,cpp}` exposant deux tables `I18nStrings` (FR/EN) et un accesseur `TR()`. `display.cpp` (textes ecran) et `wifi_manager.cpp` (interface web) referencent ces chaines au lieu de litteraux codes en dur. Le JS client recoit un petit dictionnaire `L` injecte par langue (`jsLangDict()`), pour rester independant de la langue. Les accents FR sur la matrice sont preserves via les octets de police (glcdfont_mod).
- Les messages d'echec OTA (`otaFailureReason`) et l'attribut `<html lang>` suivent egalement la langue active.

### Note de deploiement (OTA)

- Les deux langues etant compilees dans chaque binaire, le suffixe `_en` ne concerne que le **flash usine** d'un lot. Pour l'OTA, un seul binaire par variante (ex. `moduleair`) peut etre pousse a tous les capteurs classiques : chacun conserve sa langue lue en NVS. Aucun canal OTA separe par langue n'est necessaire.

## [0.3.1] - 2026-05-26

### Corrigé

- Toggle capteur (web UI) : desactiver un capteur arretait la lecture mais l'envoi serveur continuait avec les dernieres valeurs en cache jusqu'au prochain reboot. Maintenant le drapeau `_ok` est explicitement remis a false des qu'un capteur est desactive, ce qui retire son champ du JSON et masque ses ecrans matrice / ses cartes web immediatement (au prochain cycle de mesure).
- Toggle capteur : un capteur volontairement desactive ne leve plus son bit `error_flags` (BME280=0x04, NPM=0x08, MHZ19=0x80) cote serveur — le bit est gate par `enabled && !ok` au lieu de `!ok` seul, pour distinguer "panne" de "choix utilisateur".
- Ecrans matrice : `SCR_PM_ERR` et `SCR_CO2_ERR` ne s'affichent plus quand l'utilisateur a desactive le capteur correspondant (NPM ou MH-Z19) — on cache l'ecran au lieu de signaler une erreur fictive.
- Web dashboard (card Systeme) : les badges capteurs deviennent tri-state (vert "ok" / rouge "err" / gris barre "off") au lieu d'afficher en rouge un capteur juste desactive par l'utilisateur.
- Toggle NextPM / MH-Z19 : reactivation a la volee sans reboot. Pattern ESPHome / Tasmota : les UART restent ouverts en permanence au boot peu importe le toggle, et on gate uniquement la lecture. Lors d'une transition desactive->actif, drain du buffer RX + re-bind de la lib pour eviter une trame corrompue au premier cycle. La mention "Redemarrage requis" disparait de l'UI.
- Web UI : les toggles d'ecrans matrice se grisent automatiquement quand leur capteur parent est desactive (ex: si NextPM est off, les toggles PM1/PM2.5/PM10 deviennent non-cliquables avec mention "(capteur off)"). Les preferences utilisateur sont conservees — quand on reactive le capteur, les toggles d'ecrans retrouvent l'etat precedent automatiquement.
- Web UI : bannieres d'alertes et badges Systeme : ne plus afficher de fausse alerte "capteurs introuvables" pendant les ~60 premieres secondes apres reboot, le temps que le premier cycle de mesure se termine. Gate par `lastReadTime > 0`. Les badges affichent un etat "warm" (orange) pendant cette phase au lieu de "err" (rouge).
- Web UI : refresh sans rechargement de page. Pattern Tasmota / WLED : `refreshUI()` refetch silencieusement `/`, parse le HTML, et remplace uniquement la `.grid` (toutes les cartes) via `DOMParser` + `replaceWith`. Plus de flash blanc, plus de saut de scroll, le focus est preserve. Auto-refresh toutes les 30s (mise en pause si un INPUT/TEXTAREA/SELECT est focus ou si l'utilisateur a interagi dans les 5 dernieres secondes). Refresh instantane sur retour de focus de l'onglet (`visibilitychange`).
- Web UI : toggle capteur appelle `refreshUI()` au lieu de `location.reload()` pour rafraichir les effets cascade (badges, locks d'ecrans, cartes de donnees) sans flash de page.
- OTA : ecran matrice reste lit pendant toute la mise a jour (avant : noir entre les paliers car le refresh ISR etait mis en pause)
- OTA : redesign des ecrans matrice. Pendant le download, plus de barre de progression (qui scintillait a chaque palier) : remplacee par un spinner anti-aliase orange (mini version de l'anim de boot) sur la droite, avec a gauche "Mise a / jour / 42 %" en 3 lignes. Le spinner avance a chaque callback OTA donc reflete l'activite reseau en continu. Le texte du pourcentage n'est redessine que quand sa valeur change (1% boundary), eliminant le flicker des fillRect successifs.
- OTA : ecrans "succes" et "echec" reecrits + recales. Avant "Mise a jour" debordait de 2px a droite (texte de 66px sur une matrice 64px). Maintenant texte split sur 3 lignes parfaitement centrees via un helper `printCentered()`: "Mise a / reussie! / Reboot..." (vert+blanc) ou "Mise a / jour KO / Voir logs" (rouge+gris).
- OTA : debit du telechargement multiplie par ~10-20x grace a trois optims combinees :
  - `WiFi.setSleep(false)` pendant l'OTA (le power-save par defaut ajoute ~100-300 ms de latence par paquet)
  - `SPIFFS.end()` avant l'update (la flash SPI est single-master, chaque lecture SPIFFS stalle l'ecriture OTA)
  - `WiFiClientSecure.setTimeout(60000)` (HTTPUpdate a un timeout court hardcode qui trip sur slow start TLS)
- OTA : log "[OTA] Progress: X%" desormais limite aux paliers de 10% (avant : un log par chunk = ~400 lignes pour un download)

## [0.3.0] - 2026-05-26

### Ajouté

- Badge connectivité WiFi en haut a droite des ecrans de donnees (3 etats : connecte / connexion / hors-ligne), icone redessinee en 3 arcs
- Ecrans de connexion WiFi portes de la V2.1 : icone animee + layout 3 lignes, duree minimale d'affichage 1.8 s
- Nouvelle animation de boot : spinner circulaire avec trainee anti-aliasee et fondu progressif (1 tour de spin + 1 tour de fade)
- Ecran d'erreur dedie pour les capteurs PM (NextPM) et CO2 (MH-Z19) defaillants
- Bandeau d'alertes en haut du dashboard web pour signaler les capteurs en erreur
- Streaming des logs sur `/logs` en mode tail cursor-based avec auto-scroll intelligent, pause et clear
- Logos de boot stockes en SPIFFS (3 logos) et charges en RAM a l'init pour liberer de la flash
- Re-detection live des capteurs I2C a chaque cycle de mesure (BME280 / CCS811 / SFA40 hot-plug)
- Connexion WiFi "smart" : decision scan-based pour basculer en mode AP plutot que retry aveugle

### Modifié

- Detection d'erreur durcie pour NextPM et MH-Z19 (moins de faux positifs / faux negatifs)
- Position verticale des unites sur les ecrans de mesure (compensation auto-shift +6 du setFont)

### Corrigé

- `data_sender` : arret de l'usage detourne des bits ENVEA/NOISE pour CCS811 et SFA40 dans `error_flags`

## [0.2.9] - 2026-04-07

### Modifié

- Logger : buffer circulaire passe de 50 a 200 lignes (~24 KB RAM)
- Endpoint `/logs` : reponse en HTTP chunked (streaming) au lieu d'une grosse String en heap, pour eviter la fragmentation memoire

## [0.2.8] - 2026-04-07

### Corrige

- Code ISO HCHO : `ISO_102` -> `ISO_VB` (code LCSQA correct) dans l'envoi serveur et la doc

## [0.2.7] - 2026-04-06

### Modifié

- Rotation ecran : tous les polluants puis 1 logo (alterne entre les logos actifs a chaque cycle)

## [0.2.6] - 2026-04-06

### Corrige

- OTA : validation de la partition au boot (esp_ota_mark_app_valid) pour eviter le rollback

## [0.2.5] - 2026-04-06

### Ajouté

- Logo AtmoSud dans la rotation ecran (toggle sur le dashboard, persiste en NVS)

### Modifié

- Animation boot : cercle plus grand (rayon 12), 2 tours, easing plus prononce (ease²)

## [0.2.4] - 2026-04-06

### Corrige

- OTA : fix reboot sur ancienne partition (rebootOnUpdate false + restart manuel)
- Animation boot : pause du refresh display pour eviter le ghosting
- OTA progress : rafraichissement tous les 10% avec manualRefresh reduit (moins de scintillement)

## [0.2.3] - 2026-04-06

### Modifié

- Animation de demarrage : cercle au centre de l'ecran (au lieu du perimetre), plus courte
- Logos intercales entre les ecrans de donnees (plus de logos consecutifs)
- Suppression des logs doublons pour les ecrans logos
- Ajout du temps d'affichage dans les logs ecrans : `[Display](5s) Screen 1/7: PM1`

## [0.2.2] - 2026-04-06

### Ajouté

- Animation de demarrage : pixel colore parcourant le perimetre de l'ecran (3 tours, easing)
- Ecrans matrice pendant le provisioning BLE : connexion, identifiants recus, tentative WiFi, succes/echec, reboot
- Ecran "air interieur" affiche au debut de chaque cycle de mesure (60s)
- SFA40 (HCHO) documente dans le README : tableau capteurs, mesures, JSON, error_flags

### Modifié

- Ecran Mode AP simplifie : "Config / WiFi..." (sans SSID ni IP)
- Ecran HCHO : valeur affichee en entier (sans decimale)

## [0.2.1] - 2026-04-06

### Ajouté

- Ecran formaldehyde (HCHO) sur la matrice LED avec seuils couleur (10/30/100 ppb)
- Carte formaldehyde sur le dashboard web, badge SFA40 dans Systeme
- Toggles SFA40 (capteurs) et Formaldehyde (ecrans) sur le dashboard

## [0.2.0] - 2026-04-06

### Ajouté

- **Configuration WiFi via Bluetooth (BLE Improv)** : page web moduleair.fr/connect, compatible Chrome/Edge, coexiste avec le portail captif AP pour iOS
- **Capteur formaldehyde SFA40** (DFRobot SEN0661) : driver I2C natif, CRC-8 Sensirion, mesure HCHO en ppb
- Carte formaldehyde sur le dashboard web, badge SFA40 dans Systeme
- Toggle SFA40 (capteurs actifs) et Formaldehyde (ecrans matrice) sur le dashboard
- Envoi formaldehyde au serveur (ISO_102), bit 5 error_flags pour SFA40
- Page BLE : formulaire WiFi inline, liste des reseaux WiFi via notifications BLE, ecran de succes avec liens IP + mDNS
- NimBLE-Arduino pour le stack BLE (leger en RAM/Flash)

### Corrige

- Scan WiFi BLE : envoi par notifications individuelles (compatible tout MTU)
- Provisioning BLE : nettoyage des event listeners, verification etat post-connexion

## [0.1.6] - 2026-04-06

### Ajouté

- Seuils d'alerte CO2 editables depuis le dashboard (bon/mauvais, persistes en NVS)
- Bouton "Par defaut" pour restaurer les seuils officiels (800/1500 ppm)
- Ecran logo AirCarto dans la rotation matrix (activable/desactivable)
- Toggles logos (ModuleAir, AirCarto) et polluants separes en 2 categories sur l'UI
- Luminosite ecran reglable jusqu'a 0 (ecran eteint) avec warning
- Simplification du flow WiFi au demarrage (suppression logo/reconnexion parasites)

## [0.1.5] - 2026-04-06

### Ajouté

- Controle luminosite ecran matrix via slider sur le dashboard (10-255, persiste en NVS)
- Reduction du scintillement pendant OTA : refresh display pause, rafraichissement manuel uniquement sur changement de progression

## [0.1.4] - 2026-04-06

### Ajouté

- Logs `[Display]` pour tous les ecrans : logo, debug splash, WiFi connecting/connected/lost, AP mode, OTA update/done/failed, rotation des donnees

## [0.1.3] - 2026-04-06

### Ajouté

- Toggles "Capteurs actifs" sur le dashboard : activer/desactiver NextPM, MH-Z19, BME280, CCS811 (persiste en NVS, redemarrage requis)
- Toggles "Ecrans matrice" sur le dashboard : choisir quels polluants afficher sur l'ecran (PM1, PM2.5, PM10, CO2, temperature, humidite, COV), effet immediat
- Ecran PM1 sur la matrice
- Module settings (settings.h/settings.cpp) pour la gestion centralisee des preferences capteurs et ecrans
- Logs des changements d'ecran matrice (`[Display] Screen X/Y: ...`)

## [0.1.2] - 2026-04-06

### Ajouté

- Affichage cyclique des polluants sur l'ecran matrix (PM2.5, PM10, CO2, temperature, humidite, COV) avec codes couleur et messages status
- Logo ModuleAir affiche en attente de la premiere mesure apres connexion WiFi
- Ecrans OTA sur la matrice : progression avec barre, succes/echec
- BLE Improv WiFi : configuration WiFi via Bluetooth (NimBLE)

### Modifie

- Ecran "Connecte" simplifie (SSID + signal, sans IP), affiche 3s puis retour au logo

## [0.1.1] - 2026-04-06

### Ajouté

- Suivi WiFi sur l'ecran matrix : connexion en cours (SSID + points animes), connecte (IP + barres signal), mode AP (SSID AP + IP), WiFi perdu
- Option "Ecran debug au demarrage" configurable depuis le dashboard web (persistee en NVS)
- Documentation des ecrans matrix (`docs/DISPLAY_SCREENS.md`) : flow, schemas ASCII, transitions

### Modifie

- Splash screen debug passe de flag compile-time (`-D DISPLAY_DEBUG_SPLASH`) a preference runtime via l'UI web

## [0.1.0] - 2026-04-06

### Ajouté

- Ecran Matrix LED HUB75 64x32 (PxMatrix) avec affichage du logo ModuleAir au demarrage
- Splash screen debug optionnel
- Bouton "Rafraichir" sur le dashboard web
- Documentation hardware complete (`docs/HARDWARE.md`) : schematic, PCB, pinout, BOM

### Modifie

- Layout du dashboard web ameliore pour grands ecrans (header flex, grid 3 colonnes)

### Supprime

- Bandeau LEDs WS2812B (led.cpp, led.h) et dependance Adafruit NeoPixel
- Section LEDs du dashboard web (on/off, luminosite)
- Pin IO25 libere pour l'ecran Matrix (P_LAT)

## [0.0.1] - 2026-04-06

### Ajouté

- Initial commit basé sur ModuleAir Light v0.9.5
