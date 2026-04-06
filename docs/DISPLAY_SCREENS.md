# Ecrans Matrix LED — ModuleAir V4

Documentation des ecrans affiches sur la matrice LED HUB75 64x32 pixels selon l'etat du capteur.

La luminosite de l'ecran est reglable depuis le dashboard web (Systeme > slider 10-255).

## Flow de demarrage

```
  ┌─────────────────┐
  │     BOOT ESP32   │
  └────────┬─────────┘
           │
           ▼
  ┌─────────────────┐     oui     ┌─────────────────┐
  │  Debug splash   │◄────────────│  Option activee  │
  │  activee ?      │             │  dans l'UI web   │
  └────────┬────────┘             └──────────────────┘
           │ non
           ▼
  ┌─────────────────┐     oui     ┌─────────────────┐
  │  Credentials    │────────────►│  Ecran CONNEXION │
  │  WiFi sauves ?  │             └────────┬─────────┘
  └────────┬────────┘                      │
           │ non                     ┌─────┴──────┐
           │                         │             │
           │                      succes         echec
           │                         │             │
           │                         ▼             │
           │                    ┌──────────┐       │
           │                    │ CONNECTE │       │
           │                    │ (3s)     │       │
           │                    └────┬─────┘       │
           │                         │             │
           │                         ▼             │
           │                    ┌──────────┐       │
           │                    │  LOGO    │       │
           │                    │(attente  │       │
           │                    │ mesures) │       │
           │                    └────┬─────┘       │
           │                         │             │
           │                    1ere mesure        │
           │                         │             │
           │                         ▼             │
           │                    ┌──────────┐       │
           │                    │ ROTATION │       │
           │                    │ DONNEES  │       │
           │                    │ (5s/ecran│       │
           │                    └──────────┘       │
           │                                       │
           ▼                                       ▼
  ┌─────────────────┐                    ┌─────────────────┐
  │  Ecran MODE AP  │◄───────────────────│                 │
  └─────────────────┘                    └─────────────────┘
```

## Ecrans de reference

### Debug Splash (optionnel, 5s)

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│    M o d u l e A i r   V 4                                     │
│                                                                │
│              v 0 . 1 . 5                                       │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       bleu                    gris
```

Active/desactive depuis le dashboard web (Systeme > "Ecran debug au demarrage").

### Connexion en cours

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│    C o n n e x i o n                                           │
│                                                                │
│    M a B o x W i F i                                           │
│                                                                │
│    . . .                                                       │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       bleu                   blanc                  gris
```

- Le SSID est tronque a 10 caracteres si necessaire
- Les points s'animent : `.` → `..` → `...` → (vide) → `.` ...

### Connecte (3s)

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│    C o n n e c t e               ▮ ▮ ▮ ▮                      │
│                                                                │
│    M a B o x W i F i                                           │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       vert          barres signal    blanc
```

Barres de signal (4 barres) :
- 1 barre rouge : < -70 dBm (faible)
- 2 barres orange : -70 a -60 dBm (moyen)
- 3 barres vertes : -60 a -50 dBm (bon)
- 4 barres vertes : > -50 dBm (excellent)

### Logo ModuleAir (attente mesures)

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│            Bitmap RGB565 64x32                                 │
│            logo_moduleair[]                                    │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

Affiche apres la connexion WiFi en attendant la premiere mesure des capteurs.

### Mode AP

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│    C o n f i g                                                 │
│                                                                │
│    W i F i . . .                                               │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       orange                  blanc
```

Indique que le capteur attend une configuration WiFi (via le portail captif ou BLE).

## Ecrans de donnees (rotation automatique)

Apres la premiere mesure, l'ecran alterne entre les polluants toutes les **5 secondes**. Seuls les capteurs detectes et les ecrans actives dans le dashboard sont affiches. Le logo ModuleAir apparait en fin de chaque cycle.

Les ecrans affichables sont configurables depuis le dashboard (Ecrans matrice).

### Layout generique d'un ecran de donnees

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│    P M 2 . 5   u g / m 3                                      │
│                                                                │
│        1 2          ┌──────────┐                               │
│                     │  couleur │                               │
│                     └──────────┘                               │
│              B o n                                             │
│                                                                │
└────────────────────────────────────────────────────────────────┘
  label: cyan   unit: gris   valeur: blanc (x2)   carre: couleur
                             message status: couleur
```

### Palette de couleurs

| Nom | Code RGB565 | RGB | Hex | Utilisation |
|-----|-------------|-----|-----|-------------|
| Vert | `0x07E0` | `(0, 255, 0)` | `#00FF00` | Bon / OK / Ideal |
| Jaune | `0xFFE0` | `(255, 255, 0)` | `#FFFF00` | Moyen |
| Orange | `0xFBE0` | `(255, 124, 0)` | `#FF7C00` | Degrade / Aerer |
| Rouge | `0xF800` | `(255, 0, 0)` | `#FF0000` | Mauvais / Chaud / Sec / Humide |
| Bleu | - | `(0, 0, 255)` | `#0000FF` | Froid (temperature uniquement) |
| Cyan | `0x07FF` | `(0, 255, 255)` | `#00FFFF` | Labels polluants |
| Blanc | `0xFFFF` | `(255, 255, 255)` | `#FFFFFF` | Valeurs numeriques |
| Gris | `0x6B4D` | `(107, 107, 107)` | `#6B6B6B` | Unites, texte secondaire |

### Seuils de couleur et messages

Les seuils du CO2 sont editables depuis le dashboard (Seuils d'alerte). Les autres sont fixes.

#### PM2.5 (ug/m3)

| Plage | Couleur | Message |
|-------|---------|---------|
| < 10 | Vert | Bon |
| 10 - 20 | Jaune | Moyen |
| 20 - 50 | Orange | Degrade |
| >= 50 | Rouge | Mauvais |

#### PM10 (ug/m3)

| Plage | Couleur | Message |
|-------|---------|---------|
| < 15 | Vert | Bon |
| 15 - 30 | Jaune | Moyen |
| 30 - 75 | Orange | Degrade |
| >= 75 | Rouge | Mauvais |

#### PM1 (ug/m3)

| Plage | Couleur | Message |
|-------|---------|---------|
| < 10 | Vert | Bon |
| 10 - 20 | Jaune | Moyen |
| 20 - 50 | Orange | Degrade |
| >= 50 | Rouge | Mauvais |

#### CO2 (ppm) — seuils editables, defaut 800/1500

| Plage | Couleur | Message |
|-------|---------|---------|
| < 800 | Vert | Bon |
| 800 - 1500 | Orange | Aerer SVP |
| >= 1500 | Rouge | Mauvais |

#### Temperature (C)

| Plage | Couleur | Message |
|-------|---------|---------|
| < 19 | Bleu | Froid |
| 19 - 28 | Vert | OK |
| >= 28 | Rouge | Chaud |

#### Humidite (%)

| Plage | Couleur | Message |
|-------|---------|---------|
| < 40 | Rouge | Sec |
| 40 - 60 | Vert | Ideal |
| >= 60 | Rouge | Humide |

#### COV / TVOC (ppb)

| Plage | Couleur | Message |
|-------|---------|---------|
| < 220 | Vert | Bon |
| 220 - 660 | Jaune | Moyen |
| 660 - 2200 | Orange | Degrade |
| >= 2200 | Rouge | Mauvais |

#### HCHO (ppb)

| Plage | Couleur | Message |
|-------|---------|---------|
| < 10 | Vert | Bon |
| 10 - 30 | Jaune | Moyen |
| 30 - 100 | Orange | Degrade |
| >= 100 | Rouge | Mauvais |

## Ecrans BLE (configuration WiFi via Bluetooth)

Affiches sur la matrice pendant le flux de provisioning BLE (depuis la page web de configuration).

### Flow BLE

```
  ┌──────────────────┐
  │  Client BLE      │
  │  se connecte     │
  └────────┬─────────┘
           │
           ▼
  ┌──────────────────┐
  │  BLE CONNECTE    │
  └────────┬─────────┘
           │
           ▼
  ┌──────────────────┐
  │  IDENTIFIANTS    │
  │  RECUS           │
  └────────┬─────────┘
           │
           ▼
  ┌──────────────────┐
  │  CONNEXION WiFi  │
  │  EN COURS        │
  └────────┬─────────┘
           │
     ┌─────┴──────┐
     │             │
     ▼             ▼
  succes         echec
     │             │
     ▼             ▼
  ┌──────────┐  ┌──────────┐
  │ WiFi OK  │  │ WiFi     │
  │          │  │ Echec    │
  └────┬─────┘  └────┬─────┘
       │              │
       ▼              ▼
  ┌──────────┐  ┌──────────┐
  │ Config OK│  │ Retour   │
  │ Reboot...│  │ Mode AP  │
  └────┬─────┘  └──────────┘
       │
       ▼
    Reboot
```

### BLE Connecte

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│              B L E                                             │
│                                                                │
│    C o n n e c t e                                             │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       cyan                    blanc
```

### Identifiants recus

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│    I d e n t i f i a n t                                       │
│                                                                │
│       r e c u s                                                │
│                                                                │
│    M a B o x W i F i                                           │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       cyan              orange               blanc
```

### Connexion WiFi en cours

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│    C o n n e x i o n                                           │
│                                                                │
│    M a B o x W i F i                                           │
│                                                                │
│    . . .                                                       │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       bleu                   blanc                  gris
```

### WiFi OK

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│       W i F i   O K !                                          │
│                                                                │
│    M a B o x W i F i                                           │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       vert                    blanc
```

### WiFi Echec

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│          W i F i                                               │
│                                                                │
│        E c h e c !                                             │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       rouge                   orange
```

Affiche 2 secondes, puis retour a l'ecran Mode AP.

### Config OK / Reboot

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│    C o n f i g   O K                                           │
│                                                                │
│      R e b o o t . . .                                         │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       vert                    blanc
```

## WiFi perdu (en fonctionnement)

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│          W i F i                                               │
│                                                                │
│    D e c o n n e c t e                                         │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       rouge                  orange
```

Affiche quand la connexion WiFi est perdue. Retour a la rotation des donnees quand le WiFi revient.

## Ecrans OTA (mise a jour firmware)

Declenche depuis le dashboard web via "Verifier les mises a jour" > "Mettre a jour".

Le refresh de l'ecran est mis en pause pendant le telechargement pour eviter le scintillement (le WiFi SSL et le SPI display partagent le core 0).

### Flow OTA

```
  ┌──────────────────┐
  │  Clic "Mettre    │
  │  a jour" sur UI  │
  └────────┬─────────┘
           │
           ▼
  ┌──────────────────┐
  │  TELECHARGEMENT  │  (refresh pause)
  │  + barre progres │
  └────────┬─────────┘
           │
     ┌─────┴──────┐
     │             │
     ▼             ▼
  succes         echec
     │             │
     ▼             ▼
  ┌──────────┐  ┌──────────┐
  │ OTA OK   │  │ OTA FAIL │
  │ (2s)     │  │(refresh  │
  └────┬─────┘  │ reprend) │
       │        └──────────┘
       ▼
    Reboot
```

### Telechargement en cours

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│    M i s e   a   j o u r                                       │
│                                                                │
│              4 7 %                                             │
│                                                                │
│    ┌──────────────────────────────────────────────────────┐    │
│    │████████████████████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░│    │
│    └──────────────────────────────────────────────────────┘    │
└────────────────────────────────────────────────────────────────┘
       orange         blanc           barre: orange → vert a 50%
```

### Mise a jour reussie (2s puis reboot)

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│    M i s e   a   j o u r                                       │
│                                                                │
│       O K   !   R e b o o t                                    │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       vert                    blanc
```

### Mise a jour echouee

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│    M i s e   a   j o u r                                       │
│                                                                │
│          E c h e c   !                                         │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       rouge                   orange
```

## Resume des transitions

| Evenement | Ecran affiche | Duree |
|-----------|---------------|-------|
| Boot | Debug splash (si active) | 5s |
| Tentative connexion WiFi | "Connexion" + SSID + points | Jusqu'au resultat (max 15s) |
| Connexion reussie | "Connecte" + SSID + signal | 3s |
| Attente premiere mesure | Logo ModuleAir | Jusqu'a la 1ere mesure (~60s) |
| Fonctionnement normal | Rotation PM1/PM2.5/PM10/CO2/Temp/Humi/COV/HCHO/Logo | 5s par ecran |
| Pas de credentials / echec | "Mode AP" + SSID AP + IP | Permanent |
| Perte WiFi en fonctionnement | "WiFi Deconnecte" | Jusqu'a reconnexion |
| Reconnexion WiFi | Retour a la rotation des donnees | - |
| BLE client connecte | "BLE / Connecte" | Jusqu'a reception credentials |
| BLE identifiants recus | "Identifiant / recus / SSID" | Jusqu'a tentative WiFi |
| BLE connexion WiFi | "Connexion / SSID / ..." | Jusqu'au resultat (max 15s) |
| BLE WiFi OK | "WiFi OK! / SSID" | 1s |
| BLE WiFi echec | "WiFi / Echec!" | 2s puis retour Mode AP |
| BLE reboot | "Config OK / Reboot..." | 1s puis reboot |
| OTA telechargement | "Mise a jour" + barre + % (refresh pause) | Duree du telechargement |
| OTA succes | "OK ! Reboot" | 2s puis reboot |
| OTA echec | "Echec !" | Permanent (retour dashboard) |
