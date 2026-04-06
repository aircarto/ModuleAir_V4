# Changelog

Toutes les modifications notables du firmware ModuleAir Light sont documentées ici.

Format basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/), versioning [Semantic Versioning](https://semver.org/).

## [0.9.5] - 2026-04-03

### Modifié

- Bump de version pour test OTA

## [0.9.4] - 2026-03-24

### Modifié

- Remplacement de `error_msg` (string) par les bitmasks conformes à la doc : `error_flags`, `npm_status`, `device_status`
- Stockage du registre statut NextPM dans `npmStatus`
- Mapping error_flags : BME280 (bit 2), NextPM (bit 3), CCS811 (bit 4/ENVEA), MH-Z19 (bit 7/WIND)

## [0.9.3] - 2026-03-24

### Ajouté

- Dashboard responsive : layout en grille (2 colonnes tablette, 3 colonnes desktop) pour exploiter les grands écrans
- Envoi de la version firmware dans le JSON (`version_major`, `version_minor`, `version_patch`)

## [0.9.2] - 2026-03-24

### Ajouté

- **Capteur CCS811 (CJMCU-811)** : mesure des COV (TVOC en ppb) et eCO2 (ppm) via I2C
- Compensation automatique température/humidité depuis le BME280
- Carte "COV (CCS811)" sur le dashboard web avec seuils colorés
- Envoi TVOC (ISO_100) et eCO2 (ISO_101) vers le serveur de données
- Badge capteur CCS811 dans la section Système du dashboard

### Modifié

- URL du serveur OTA : `gestion.aircarto.fr` (anciennement `admin.aircarto.fr`)

## [0.9.1] - 2026-03-10

### Amélioré

- Dashboard : signal WiFi affiché avec barres SVG colorées + label (Excellent/Bon/Moyen/Faible) au lieu de la valeur brute en dBm
- Page AP : bouton "Actualiser les réseaux" pour rescanner le WiFi sans recharger la page
- Page AP : bouton oeil pour afficher/masquer le mot de passe WiFi

## [0.9.0] - 2026-03-10

### Ajouté

- **Détection perte WiFi** : damier rouge clignotant quand la connexion WiFi est perdue, reconnexion automatique, retour au mode normal à la reconnexion
- **Détection absence d'internet** : damier orange clignotant si le WiFi est connecté mais sans accès internet (test DNS `clients3.google.com`)
- **Détection serveur indisponible** : damier violet clignotant si internet OK mais le serveur `data.moduleair.fr` ne répond pas
- Les LEDs d'erreur restent actives jusqu'au prochain envoi réussi
- L'envoi de données est désormais bloqué quand le WiFi est déconnecté (évite les requêtes inutiles)

## [0.8.6] - 2026-03-09

### Modifié

- `device_id` envoyé directement en MAC hex (`"10AD94DC5194"`) au lieu du double encodage hex ASCII (`"313041443934444335313934"`)
- Affichage de la date/heure du serveur dans les logs après chaque envoi de données

## [0.8.5] - 2026-03-09

### Ajouté

- **Logs consultables sur le dashboard web** : carte "Logs" avec les 50 dernières lignes, auto-refresh toutes les 5 secondes
- Endpoint `/logs` retournant le buffer circulaire en texte brut
- Module `logger` : classe `LoggerPrint` (hérite de `Print`) écrivant simultanément sur Serial et buffer circulaire (50 lignes x 120 caractères)

### Modifié

- Tous les modules utilisent désormais `Logger` au lieu de `Serial` pour les logs (config, sensors, data_sender, led, wifi_manager)

## [0.8.4] - 2026-03-09

### Amélioré

- Page de confirmation WiFi : explication des indicateurs lumineux (bleu = connecté, orange = échec)

## [0.8.3] - 2026-03-09

### Amélioré

- **LEDs en mode connecté** : respiration bleue continue entre les mesures (si activées)
- Clignotements bleus rapides pendant 2 secondes lors de l'envoi des données
- Animation de connexion WiFi : clignotements bleus francs pendant 5 secondes
- Page de redémarrage avec redirection automatique (plus de boucle de reboot au refresh)

## [0.8.2] - 2026-03-09

### Ajouté

- **Contrôle des LEDs** depuis le dashboard web : bouton on/off + slider de luminosité (5-255)
- État des LEDs et luminosité persistés en NVS (conservés après redémarrage)
- Animation bleue de 5 secondes à la connexion WiFi réussie
- LEDs éteintes par défaut en mode connecté (économie d'énergie)

### Modifié

- Respiration orange/rouge en mode AP (au lieu de orange)
- Scan WiFi uniquement en mode AP (plus de scan inutile en mode connecté)

## [0.8.1] - 2026-03-09

### Ajouté

- Bouton **"Redémarrer le capteur"** sur le dashboard web
- Envoi de `current_version` dans les requêtes OTA pour suivi serveur

### Modifié

- Séparateur visuel entre chaque cycle de mesure dans les logs série
- Réponse serveur affichée dans les logs d'envoi de données
- Temps de réponse affiché en secondes

## [0.8.0] - 2026-03-09

### Ajouté

- **Envoi des données vers le serveur** AirCarto toutes les 60 secondes
- Module `data_sender` : POST JSON vers `data.moduleair.fr` avec format ISO LCSQA
- Données envoyées : PM1/PM2.5/PM10 (ISO_68/39/24), CO2 (ISO_17), température/humidité/pression (ISO_54/55/53), signal WiFi
- Device ID encodé en hexadécimal ASCII dans le JSON
- Gestion des erreurs capteurs via le champ `error_msg`

### Modifié

- Cycle de mesure simplifié : lecture capteurs + envoi serveur toutes les 60 secondes (au lieu de lectures toutes les 10s)
- Logs d'envoi allégés : URL, JSON, code HTTP et durée uniquement

### Corrigé

- Le premier envoi de données attend maintenant la première lecture capteur (évite un HTTP 400 au boot)

## [0.7.0] - 2026-03-09

### Ajouté

- **Mise à jour OTA** (Over-The-Air) depuis un serveur distant
- Bouton "Vérifier les mises à jour" sur le dashboard web
- Comparaison de version automatique (`version.txt` sur le serveur)
- Téléchargement et installation du firmware (`firmware.bin`) avec confirmation utilisateur
- Logs détaillés du processus OTA (vérification, téléchargement, résultat)
- Envoi du Device ID (`?sensor=AABBCCDDEEFF`) lors des requêtes OTA pour suivi serveur
- Script `ota_upload.py` pour upload automatique du firmware à chaque build
- Support HTTPS (WiFiClientSecure) pour les requêtes OTA

### Modifié

- Table de partitions `min_spiffs.csv` (plus d'espace pour les deux partitions OTA)
- Device ID simplifié : `AABBCCDDEEFF` (MAC seule, sans préfixe `esp32-`)

## [0.6.0] - 2026-03-09

### Ajouté

- **Device ID unique** basé sur l'adresse MAC (format `esp32-AABBCCDDEEFF`, compatible NebuleAir)
- SSID du point d'accès unique par capteur (`ModuleAirLight-XXXXXX`)
- Device ID affiché au démarrage (logs série) et sur la page web (section Système)
- **Dashboard web** en mode connecté : dernières mesures PM/CO2/temp/humidité/pression, état des capteurs, infos système (ESP32, RAM, uptime)
- Indicateurs colorés sur les mesures (vert/orange/rouge selon les seuils)
- Sélection WiFi améliorée : clic sur un réseau → formulaire mot de passe inline

### Modifié

- La page web en mode connecté n'affiche plus la liste des réseaux WiFi (remplacée par le dashboard)

## [0.5.0] - 2026-03-09

### Ajouté

- **Captive portal** : la page de configuration s'ouvre automatiquement à la connexion au réseau du capteur
- Serveur DNS redirigeant toutes les requêtes vers l'AP
- Endpoints de détection captive portal (Android, iOS, Windows)
- Redirection des URLs inconnues vers la page de config en mode AP

### Amélioré

- Scan WiFi asynchrone (plus de blocage lors du chargement de la page)
- Envoi de la page web par chunks (plus de timeout / "Connection reset by peer")
- CSS stocké en PROGMEM (économie de RAM)
- Refactoring du code en modules séparés (config, led, sensors, wifi_manager)

## [0.4.0] - 2026-03-09

### Ajouté

- **WiFi Manager** : mode AP (SSID: ModuleAirLight) avec page web de configuration
- Scan des réseaux WiFi avec affichage de la force du signal
- Sauvegarde SSID/mot de passe en mémoire NVS (persistant)
- Accès à la page de config via IP locale ou **moduleair-light.local** (mDNS)
- Bouton "Oublier le WiFi" pour revenir en mode AP
- Reconnexion automatique en cas de perte WiFi
- Logs détaillés du WiFi Manager (clients AP, requêtes web, état connexion)
- Les capteurs ne démarrent que lorsque le WiFi est connecté

## [0.3.0] - 2026-03-09

### Ajouté

- **Bandeau LEDs WS2812B** (8 LEDs sur IO25)
- Animation chenillard arc-en-ciel au démarrage
- Respiration continue bleue (connecté) ou orange (mode AP)
- Impulsion lumineuse à chaque lecture capteur
- Animations non-bloquantes (machine à états avec millis())

## [0.2.0] - 2026-03-09

### Ajouté

- Capteur CO2 **MH-Z19** via UART2 (RX=IO36, TX=IO27)
- Capteur **BME280** via I2C (température, humidité, pression atmosphérique)
- Affichage de la version firmware au démarrage

## [0.1.0] - 2026-03-06

### Ajouté

- Lecture du capteur **NextPM** via Modbus RTU (PM1, PM2.5, PM10, température et humidité internes)
- Affichage des mesures sur le port série toutes les 10 secondes
