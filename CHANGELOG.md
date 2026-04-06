# Changelog

Toutes les modifications notables du firmware ModuleAir V4 sont documentées ici.

Format basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/), versioning [Semantic Versioning](https://semver.org/).

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
