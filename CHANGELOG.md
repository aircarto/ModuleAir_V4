# Changelog

Toutes les modifications notables du firmware ModuleAir V4 sont documentées ici.

Format basé sur [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/), versioning [Semantic Versioning](https://semver.org/).

## [0.6.0] - 2026-07-28

### Ajouté

- **Environnement PlatformIO `atmosud_display`** — « AtmoSud vitrine » : le logo
  AtmoSud est ON par défaut dans la rotation matrice (`-DBUILD_ATMOSUD`), mais le
  capteur **n'envoie rien à AtmoSud/MicroSpot** — pas de `-DFACTORY_ATMOSUD=1`,
  donc aucun stamp NVS n'est écrit et les données partent chez AirCarto seul.
  C'est la seule différence avec `atmosud_provision`, qui stampe la carte à vie.
  Flash USB uniquement (`extra_scripts` sans `post:ota_upload.py`), comme les
  envs P3 : ce binaire pose un défaut de 1er boot différent du binaire universel.
  **Limite** : le stamp est une propriété de la CARTE, pas du binaire — flasher
  cet env sur un capteur déjà provisionné AtmoSud ne le dé-stampe pas.

### Supprimé

- **Carte « Capteur CO2 » entièrement retirée de l'interface web** — sélecteur
  `Auto / MH-Z19 / SenseAir S8-S88` (introduit en 0.5.0) *et* affichage du modèle
  détecté. L'auto-détection fait le travail seule : le choix manuel n'apportait
  qu'un moyen de se tromper (forcer le mauvais protocole coupait la voie CO2 en
  silence), et le modèle branché n'a aucun intérêt pour l'utilisateur. Le toggle
  CO2 de « Capteurs actifs » suffit : il coupe la voie entière, quel que soit le
  modèle. Supprimés avec : route `POST /set-co2-sensor`, `settingsSetCo2Sensor()`,
  `settingsCo2SensorLabel()`, l'enum `Co2SensorChoice`, le champ
  `SensorSettings::co2_sensor` et les 4 chaînes i18n FR/EN de la carte.
- **Environnements PlatformIO `moduleair_s8` et `atmosud_provision_s8`** et le flag
  `-DDEFAULT_CO2_S88` associé. Il n'y a plus de build « spécial SenseAir » : tous
  les envs restants fonctionnent indifféremment avec un MH-Z19 ou une SenseAir, y
  compris le binaire OTA universel. Plus besoin de savoir quel capteur est monté
  avant de flasher.
- **Champ `co2_sensor` (le réglage) dans `/api/config`.** `co2_sensor_detected`
  (le résultat de détection) est conservé.

### Modifié

- `readCO2()` (sensors.cpp) repasse en auto-détection pure : sonde MH-Z19 puis
  SenseAir, mémorise le gagnant, et re-détecte après `CO2_REDETECT_AFTER` (3)
  échecs consécutifs — la re-détection redevient donc **inconditionnelle**, elle
  était désactivée en mode forcé depuis 0.5.0.
- `s88_read` (`/api/config`) ne dépend plus que de la détection : voie CO2 active
  **et** SenseAir qui répond.

### Migration

- **Aucune action.** La clé NVS `sensors/co2sel` n'est plus ni lue ni écrite ; sur
  une carte déjà provisionnée elle reste en place, orpheline et sans effet.
- Une carte flashée en 0.5.0 avec `moduleair_s8` / `atmosud_provision_s8` (donc
  `co2sel = 2`, SenseAir forcée) repasse en auto-détection après l'OTA et
  re-détecte sa SenseAir au premier cycle de lecture. Aucune interruption de
  mesure attendue.

### Inchangé

- L'envoi des données reste identique quel que soit le capteur : `ISO_17`
  (AirCarto), `MHZ16_CO2` (AtmoSud) et `MHZ19_CO2` (dashboard local) partent
  toujours depuis le même champ `data.co2`. Aucun impact côté serveurs.
- Lecture Modbus RTU SenseAir (registre `0x0003`, CRC16) et lecture MH-Z19
  inchangées, de même que l'ABC de chaque capteur.

## [0.5.0] - 2026-07-21

### Ajouté

- **Sélecteur de capteur CO2 dans l'interface web** (carte « Capteur CO2 », FR/EN) :
  `Auto (détection)` / `MH-Z19` / `SenseAir S8 / S88`. La lecture des deux modèles
  existait déjà (cf. 0.4.0) mais uniquement en auto-détection, sans moyen de la
  forcer ni de savoir quel capteur répondait. En mode `Auto`, la carte affiche
  désormais le capteur **effectivement détecté** ; en mode forcé, le protocole est
  imposé et l'autre n'est jamais sondé.
- **Persistance NVS du choix de capteur** (namespace `sensors`, clé `co2sel`), avec
  gravure du défaut compile-time au 1er boot (sentinelle `0xFF`) — même mécanique
  que la langue (`i18n/lang`) et la dalle P3 (`display/panelP3`). La NVS survivant
  à l'OTA, une carte SenseAir provisionnée en usine **garde son réglage** après
  réception du binaire OTA universel (qui reste en `Auto` par défaut). Le réglage
  reste modifiable à tout moment depuis l'UI.
- 2 environnements PlatformIO `moduleair_s8` et `atmosud_provision_s8`
  (`-DDEFAULT_CO2_S88`) : mêmes builds que `moduleair` / `atmosud_provision` avec
  le capteur CO2 par défaut sur SenseAir. **Flash USB uniquement** — leur
  `extra_scripts` exclut `post:ota_upload.py`, sinon une carte MH-Z19 neuve (NVS
  vierge) démarrerait forcée en SenseAir. Utiles pour garantir une lecture
  déterministe dès le 1er cycle sur un parc dont on connaît le matériel, sans
  dépendre de la sonde.
- Champs `co2_sensor` (réglage) et `co2_sensor_detected` (résultat de détection)
  dans `/api/config`.

### Modifié

- **Pas de re-détection en mode forcé** (`readCO2`) : le hot-swap après
  `CO2_REDETECT_AFTER` échecs consécutifs ne s'applique plus qu'en mode `Auto`.
  En mode forcé, l'utilisateur a déclaré quel capteur est monté — un silence est
  une panne ou un warm-up, pas un changement de matériel — donc partir sonder
  l'autre protocole serait faux.
- Un changement de capteur depuis l'UI **draine le RX et repart d'une détection
  vierge**, pour qu'un reliquat de trame du protocole précédent ne puisse pas être
  relu comme une réponse valide.
- `s88_read` dans `/api/config` (champ legacy Next-Gen jusqu'ici câblé en dur à
  `false`) reflète désormais la réalité : voie CO2 active **et** SenseAir qui
  répond (forcée, ou détectée en mode `Auto`).

### Inchangé (volontairement)

- **L'envoi des données est strictement identique** quel que soit le capteur : les
  deux modèles écrivent dans le même champ `data.co2`, donc les clés
  `ISO_17` (AirCarto), `MHZ16_CO2` (AtmoSud) et `MHZ19_CO2` (dashboard local)
  partent inchangées. Aucun impact côté serveurs.
- L'ABC (auto-calibration) de la SenseAir reste à son réglage d'usine (180 h,
  `HR32` @`0x001F`). `mhz19.autoCalibration(false)` ne concerne que le MH-Z19
  (commande propriétaire 0x79) : les deux capteurs ont donc des comportements de
  dérive différents — décision assumée, à revoir si besoin.

## [0.4.1] - 2026-07-07

### Ajouté

- **Écran d'échec de connexion WiFi au démarrage** : quand la connexion STA
  échoue au boot, le capteur affiche désormais un écran dédié pendant ~3 s
  **avant** de basculer en mode AP, au lieu de sauter directement de
  « Connexion » à « Config WiFi » sans feedback. L'écran indique le motif,
  dérivé de la classification `reason` déjà présente dans le firmware :
  **Mdp refuse** (auth refusée), **Absent** (SSID hors de portée) ou **Echec**
  (autre / transitoire), avec le SSID visé. Traduit FR/EN. Nouvel enum public
  `WifiFailReason` (`display.h`) pour découpler l'affichage de l'enum interne
  `WifiFailGroup` du gestionnaire WiFi.

## [0.4.0] - 2026-07-07

### Sécurité

- **L'URL AtmoSud (token inclus) n'est plus dans le code source** : sortie de
  `config.h` (où elle était commitée sur le dépôt) vers `secrets.ini` (gitignore),
  injectée au build par `secrets_inject.py` (pre-script PIO porté de NebuleAir,
  cf. `secrets.example.ini`). Sans `secrets.ini`, la branche AtmoSud est
  **compile-out** : zéro trace de l'URL/token dans le binaire (vérifié par
  `strings` sur le .bin). ⚠️ L'ancien token reste dans l'historique git — à faire
  tourner côté Probesys si le dépôt est partagé.
- **La destination d'envoi n'est plus togglable** : suppression du flag NVS
  `server/atmosud` et de son API (`settingsGet/SetAtmosudEnabled`). Remplacé par
  un **stamp NVS write-once** (namespace `atmosud`, clé `stamped`), même
  architecture que NebuleAir : écrit **uniquement** par le binaire de
  provisioning usine (`pio run -e atmosud_provision -t upload`, jamais poussé
  en OTA — son `extra_scripts` exclut `ota_upload.py`). Pas d'allowlist, pas
  de toggle : un capteur AirCarto-seul **ne peut pas** être basculé sur AtmoSud
  par une OTA (quel que soit le binaire poussé) ; un capteur AtmoSud ne peut
  pas être désactivé depuis l'UI. Basculer un capteur déjà déployé = reflash
  USB avec l'env de provisioning.

### Ajouté

- **Compatibilité capteur SensAir** : prise en charge matérielle du capteur
  SensAir en plus des capteurs existants (détection/lecture au boot, intégration
  affichage matrice et carte web). Ajustements associés BLE Improv, WiFi manager
  et affichage.
- **Sous-échantillonnage capteurs + moyennage des mesures** : les capteurs sont
  lus plus fréquemment que le cycle d'envoi et les valeurs sont moyennées avant
  publication, pour des lectures plus stables et moins de bruit ponctuel.
- Env PlatformIO `atmosud_provision` : provisioning usine des capteurs AtmoSud
  (stamp NVS au 1er boot, idempotent, survit aux OTA et au reset WiFi).
- **Migration automatique** des capteurs AtmoSud déjà déployés : au boot, si
  l'ancien flag `server/atmosud == 1` est présent et le stamp absent, le stamp
  est écrit (`dataSenderInit`). Les capteurs AtmoSud en service continuent
  d'envoyer après la mise à jour, sans intervention. L'ancienne clé est laissée
  intacte (comportement correct préservé en cas de rollback firmware).
- Card web déclarative « Envoi des données » (FR/EN) : AirCarto « Actif
  (toujours) » + statut AtmoSud du capteur (« Actif (capteur provisionné) » /
  « Inactif (capteur non concerné) »). Lecture seule — permet de vérifier en
  clientèle la destination effective, sans donner le moyen de la modifier. La
  ligne AtmoSud n'apparaît que si le binaire embarque l'URL (`secrets.ini`
  présent au build).
- 2 nouveaux environnements PlatformIO « L'Air et Moi » : `lairetmoi` (FR) et `lairetmoi_en` (EN), selectionnables au flash. **Coté données, strictement identiques à la build classique** : envoi AirCarto SEUL, jamais AtmoSud (pas de `-DBUILD_ATMOSUD`, destiné à un déploiement AirCarto sans MicroSpot). La différence est un **4e logo « L'Air et Moi »** ajouté à la rotation matrice.
- Logo « L'Air et Moi » (`logo_lairetmoi`, 64×32 RGB565, repris de `ModuleAir_V2.1/logos-custom.h` où il était commenté) : nouveau slot SPIFFS dédié `LOGO_SLOT_LAM` (`/logo_lam.bin`), éditable/réinitialisable depuis l'UI web au même titre que les 3 autres logos, avec son toggle « L'Air et Moi » dans la carte « Ecrans matrice ». Les 4 logos (ModuleAir / AirCarto / AtmoSud / L'Air et Moi) sont ON par défaut au 1er boot sur cette build.
- Le 4e slot logo est **entièrement conditionné à `-DBUILD_LAIRETMOI`** (enum, buffers, struct `ScreenSettings`, rotation d'affichage, UI web) : les builds `moduleair`/`atmosud` restent inchangés (zéro octet de RAM/flash en plus), ce qui préserve la marge de heap contigu nécessaire au handshake TLS de l'OTA (cf. 0.3.2).

### Corrigé

- **Logs web incomplets : le tail `/logs` s'arrêtait à la première ligne vide du buffer** — les blocs `[AirCarto]`/`[AtmoSud]` (envoi, HTTP 200, « Envoi reussi »…) n'apparaissaient quasiment jamais dans la card Logs alors que le moniteur série les montrait. Cause : `handleLogs` streamait chaque ligne en deux `sendContent` (`line` puis `"\n"`) ; or en transfert HTTP **chunked**, `sendContent("")` émet un chunk de taille 0 = **fin de réponse**. À la première ligne vide (les `Logger.println()` de respiration entre blocs), la réponse se terminait : le navigateur fermait la connexion et le module continuait d'écrire dedans → c'était aussi l'origine du **flood `[E][WiFiClient.cpp] write(): errno: 104 "Connection reset by peer"`**. Correctif : une seule écriture par ligne, `\n` inclus (une ligne vide part comme `"\n"`, jamais comme chunk vide). En bonus, deux fois moins d'écritures TCP par tail.
- **Lignes de log tronquées à 119 caractères sur `/logs`** : les payloads JSON `[AirCarto]`/`[AtmoSud]` (~450 chars) étaient coupés dans l'UI web. Le logger **replie** désormais les lignes longues sur plusieurs slots du buffer circulaire au lieu de jeter l'excédent (segments de 119 chars, contenu intégral, zéro RAM en plus — le verrou ligne est conservé jusqu'au vrai `\n`, donc les segments restent contigus même avec les tasks concurrentes).
- `ota_upload.py` : nouvelle garde `OTA_SKIP=1 pio run …` pour builder sans pousser le binaire sur le serveur OTA (même mécanisme que le script NebuleAir) — indispensable pour les builds de vérification locale maintenant que `extra_scripts` est actif par défaut.
- **Échec systématique de l'envoi AtmoSud (`SSL - Memory allocation failed`) puis spirale mémoire (perte DNS, beacon timeout WiFi)** : `HTTPClient` a `_reuse=true` par défaut, donc après le POST AirCarto, `http.end()` gardait la session TLS **ouverte** en keep-alive (~45 Ko de buffers mbedtls retenus), et `secClient` (au scope de la fonction) n'était détruit qu'**après** `atmosudSend()`. Le handshake AtmoSud réclamait donc un 2e contexte TLS contigu pendant que le 1er vivait encore → allocation impossible sur ESP32, puis **fuite mbedtls à chaque handshake raté** ([arduino-esp32 #5781](https://github.com/espressif/arduino-esp32/issues/5781)) jusqu'à étrangler le DNS (`hostByName: DNS Failed` permanent après ~9 min) et la pile WiFi (déconnexions `reason=200 BEACON_TIMEOUT`). Correctif : le client AirCarto passe en `setReuse(false)` (comme le faisait Next-Gen, garde-fou perdu au portage) et vit dans son propre bloc `{}` — il est entièrement libéré avant l'envoi AtmoSud, plus jamais deux contextes TLS simultanés.
- **Badge réseau menteur (« deux flèches rouges ») sur les réseaux filtrants** : la sonde internet unique du network monitor (`1.1.1.1:80`) échouait en permanence derrière les box/firewalls qui bloquent le port 80 sortant (ou 1.1.1.1 lui-même), affichant « connecté sans internet » alors que les POST HTTPS passaient très bien (constaté sur capteur en prod : badge figé pendant que `data.moduleair.fr` répondait HTTP 200). Deux changements dans `network_monitor.cpp` : la sonde **serveur** (`gestion.aircarto.fr:443`) passe en premier — s'il répond, internet est prouvé par la même occasion (`NET_OK` en une seule sonde au lieu de deux dans le cas nominal) ; et la classification ne conclut `NO_INTERNET` qu'après échec de `1.1.1.1:443` PUIS du fallback `8.8.8.8:53` (host ET port indépendants — il faudrait qu'un réseau filtre les deux pour produire un faux négatif).

## [0.3.2] - 2026-06-04

### Ajouté

- Support multilingue FR / EN (i18n) en **runtime** : tous les textes des ecrans matrice ET de l'interface web sont traduits. Un seul firmware contient les deux langues. Selecteur de langue dans l'UI web (carte "Langue / Language", disponible en mode connecte ET dans le portail AP) avec effet immediat (reload de la page ; les ecrans suivent au cycle suivant).
- Persistance de la langue en **NVS** (namespace `i18n`, cle `lang`), au meme titre que les toggles capteurs/ecrans/seuils. Consequence : la langue reste changeable a la volee et **survit a l'OTA** — la mise a jour ne reecrit que la partition applicative, jamais la NVS. Un capteur configure en anglais reste donc en anglais apres chaque MAJ, sans aucune machinerie de sauvegarde/restauration.
- 4 environnements PlatformIO selectionnables au flash : `moduleair` (classique FR), `moduleair_en` (classique EN), `atmosud` (AtmoSud FR), `atmosud_en` (AtmoSud EN). Le flag de build `-DDEFAULT_LANG_EN` ne fixe que la langue de **premier boot** (quand la NVS est vierge).
- Ecran matrice "mesure air interieur" en anglais : ce splash est une image (texte grave dans les pixels, pas du texte rendu par police), donc une version anglaise du bitmap `interieur_no_connection_en` (importee de ModuleAir-Next-Gen) est selectionnee quand la langue active est EN. Le bitmap est `const` (reside en flash, pas en DRAM) pour ne pas alourdir la RAM.

### Modifié

- Architecture i18n : nouveau module `src/i18n.{h,cpp}` exposant deux tables `I18nStrings` (FR/EN) et un accesseur `TR()`. `display.cpp` (textes ecran) et `wifi_manager.cpp` (interface web) referencent ces chaines au lieu de litteraux codes en dur. Le JS client recoit un petit dictionnaire `L` injecte par langue (`jsLangDict()`), pour rester independant de la langue. Les accents FR sur la matrice sont preserves via les octets de police (glcdfont_mod).
- Les messages d'echec OTA (`otaFailureReason`) et l'attribut `<html lang>` suivent egalement la langue active.

### Corrigé

- Check OTA qui echouait en `HTTP -1` (handshake TLS impossible). Cause racine : **manque de heap contigu**. Le handshake mbedTLS reclame ~45-50 Ko d'un seul bloc, or le device plafonnait a ~40-47 Ko de plus gros bloc libre. Les bitmaps de logos/ecrans de `logos.h` etaient declares `uint16_t static` (**non-`const`**) donc residaient en **RAM** (.data) au lieu de la flash. Les passer tous en `static const` les deplace en flash (`.rodata`, mappee en lecture sur ESP32, `drawImage()` prend deja un pointeur const) : **~16 Ko de RAM liberes** (les images reellement utilisees ; les autres etaient deja eliminees par le linker). RAM globale 37,6 % -> 32,6 %, le plus gros bloc libre repasse largement au-dessus du besoin TLS, le check OTA redevient fiable. (Note : ce probleme etait pre-existant et sans rapport avec l'i18n — la RAM statique n'a pas bouge avec les tables de traduction, qui sont en flash.)

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
