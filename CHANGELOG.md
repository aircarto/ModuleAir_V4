# Changelog

Toutes les modifications notables du firmware ModuleAir V4 sont documentées ici.

Format basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/), versioning [Semantic Versioning](https://semver.org/).

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
