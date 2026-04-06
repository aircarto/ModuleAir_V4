# Changelog

Toutes les modifications notables du firmware ModuleAir V4 sont documentées ici.

Format basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/), versioning [Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-04-06

### Ajouté

- Ecran Matrix LED HUB75 64x32 (PxMatrix) avec affichage du logo ModuleAir au demarrage
- Splash screen debug optionnel (`-D DISPLAY_DEBUG_SPLASH` dans platformio.ini)
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
