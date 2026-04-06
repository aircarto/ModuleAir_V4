# Ecrans Matrix LED — ModuleAir V4

Documentation des ecrans affiches sur la matrice LED HUB75 64x32 pixels selon l'etat du capteur.

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
  ┌─────────────────┐
  │  Logo ModuleAir │  (bitmap 64x32 RGB565)
  │  ~2s            │
  └────────┬────────┘
           │
           ▼
  ┌─────────────────┐     oui     ┌─────────────────┐
  │  Credentials    │────────────►│  Ecran CONNEXION │
  │  WiFi sauves ?  │             │  (voir ci-dessous)│
  └────────┬────────┘             └──────────────────┘
           │ non
           ▼
  ┌─────────────────┐
  │  Ecran MODE AP  │
  │  (voir ci-dessous)│
  └─────────────────┘
```

## Flow de connexion WiFi

```
  ┌──────────────────┐
  │  Ecran CONNEXION │
  │                  │
  │  "Connexion"     │  (bleu)
  │  "MaBoxWiFi"     │  (blanc, SSID tronque a 10 chars)
  │  ...             │  (points animes toutes les 500ms)
  └────────┬─────────┘
           │
      ┌────┴────┐
      │ Resultat │
      └────┬────┘
           │
     ┌─────┴──────┐
     │             │
     ▼             ▼
  succes         echec
     │             │
     ▼             ▼
  ┌──────────┐  ┌──────────┐
  │ CONNECTE │  │ MODE AP  │
  │ (3s)     │  │          │
  └────┬─────┘  └──────────┘
       │
       ▼
  Fonctionnement
  normal
```

## Ecrans de reference

### Debug Splash (optionnel, 5s)

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│                                                                │
│    M o d u l e A i r   V 4                                     │
│                                                                │
│              v 0 . 1 . 0                                       │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       bleu                    gris
```

Active/desactive depuis le dashboard web (Systeme > "Ecran debug au demarrage").

### Logo ModuleAir

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│            Bitmap RGB565 64x32                                 │
│            logo_moduleair[]                                    │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

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
│    1 9 2 . 1 6 8 . 1 . 4 2                                    │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       vert          barres signal    blanc             bleu
```

Barres de signal (4 barres) :
- 1 barre rouge : < -70 dBm (faible)
- 2 barres orange : -70 a -60 dBm (moyen)
- 3 barres vertes : -60 a -50 dBm (bon)
- 4 barres vertes : > -50 dBm (excellent)

### Mode AP

```
┌────────────────────────────────────────────────────────────────┐
│                                                                │
│       M o d e     A P                                          │
│                                                                │
│      A i r - 3 B 0 1 5 C                                      │
│                                                                │
│    1 9 2 . 1 6 8 . 4 . 1                                      │
│                                                                │
└────────────────────────────────────────────────────────────────┘
       orange            blanc (centre)             gris
```

- Le SSID de l'AP est affiche (ex: "ModuleAir-3B015C", tronque aux 10 derniers chars)
- L'IP de l'AP est affichee pour faciliter la connexion

### WiFi perdu (en fonctionnement)

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

Affiche quand la connexion WiFi est perdue. Remplace par l'ecran "Connecte" quand le WiFi revient.

## Ecrans OTA (mise a jour firmware)

Declenche depuis le dashboard web via "Verifier les mises a jour" > "Mettre a jour".

### Flow OTA

```
  ┌──────────────────┐
  │  Clic "Mettre    │
  │  a jour" sur UI  │
  └────────┬─────────┘
           │
           ▼
  ┌──────────────────┐
  │  TELECHARGEMENT  │
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
  │ (2s)     │  │          │
  └────┬─────┘  └──────────┘
       │
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

- Le titre "Mise a jour" reste affiche en orange
- Le pourcentage se met a jour en temps reel au centre
- La barre de progression passe de orange (0-49%) a vert (50-100%)

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
| Boot | Logo ModuleAir | ~2s |
| Tentative connexion WiFi | "Connexion" + SSID + points | Jusqu'au resultat (max 15s) |
| Connexion reussie | "Connecte" + IP + signal | 3s |
| Pas de credentials / echec | "Mode AP" + SSID AP + IP | Permanent |
| Perte WiFi en fonctionnement | "WiFi Deconnecte" | Jusqu'a reconnexion |
| Reconnexion WiFi | "Connecte" + IP + signal | Permanent |
| OTA telechargement | "Mise a jour" + barre + % | Duree du telechargement |
| OTA succes | "OK ! Reboot" | 2s puis reboot |
| OTA echec | "Echec !" | Permanent (retour dashboard) |
