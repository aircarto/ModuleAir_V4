# Integration LoRa - ModuleAir V4

Documentation hardware pour ajouter un module LoRa au ModuleAir V4 (revision PCB future).

## Contexte

Le ModuleAir V4 actuel (rev 1.0) utilise un ESP32-WROOM-32U avec :
- Bus **HSPI** dedie a la matrice LED HUB75 (IO13/14)
- Bus **I2C** pour BME280, CCS811, SFA40 (IO21/22)
- Deux **UART** pour NextPM (IO32/39) et MH-Z19 (IO27/36)

Le bus **VSPI** (IO5/18/19/23) est totalement libre et donc disponible pour la LoRa.

Probleme actuel : ces pins ne sont pas routes sur les connecteurs externes du PCB rev 1.0. Une revision est necessaire pour exposer un header LoRa.

## Pins ESP32 a exposer

| Signal LoRa | GPIO ESP32 | Type | Notes |
|-------------|------------|------|-------|
| **SCK** | IO18 | I/O | VSPI clock |
| **MISO** | IO19 | I/O | VSPI master in |
| **MOSI** | IO23 | I/O | VSPI master out |
| **NSS / CS** | IO5 | I/O | Strapping pin - pull-up 10k recommande |
| **DIO0 (IRQ)** | IO34 | Input only | Parfait pour IRQ, pas de conflit direction |
| **RST** | IO26 | I/O | Libre |
| **DIO1** (optionnel) | IO35 | Input only | LoRaWAN class B/C |
| **DIO2** (optionnel) | IO2 | I/O | Strapping - pull-down 10k requis |

## Connecteur LoRa propose

**Type** : JST XH 2.5mm 8 broches (compact, facile a crimper, robuste)

| Pin | Signal | GPIO ou alim |
|-----|--------|--------------|
| 1 | 3.3V | Alimentation |
| 2 | GND | Masse |
| 3 | SCK | IO18 |
| 4 | MISO | IO19 |
| 5 | MOSI | IO23 |
| 6 | CS (NSS) | IO5 |
| 7 | DIO0 | IO34 |
| 8 | RST | IO26 |

Si DIO1 / DIO2 sont necessaires (LoRaWAN class B/C), passer en **10P** :

| Pin | Signal | GPIO |
|-----|--------|------|
| 9 | DIO1 | IO35 |
| 10 | DIO2 | IO2 |

## Alimentation

### Consommation typique d'un module LoRa (RFM95W / SX1276)

| Etat | Courant |
|------|---------|
| Sleep | < 1 µA |
| RX | ~12 mA |
| TX (+13 dBm) | ~30 mA |
| **TX (+20 dBm)** | **~120 mA** |
| TX (+20 dBm) pic | ~150 mA |

### Verification du LDO actuel (AMS1117-3.3)

- Capacite max : 800 mA
- Consommation actuelle (estimation) :
  - ESP32 : 100-150 mA (baseline) + pics WiFi 300-500 mA
  - Capteurs (NPM, MHZ19, BME280, CCS811, SFA40) : ~50 mA cumules
  - Matrice LED : alimentee separement en 5V (pas via LDO 3.3V)
- **Marge restante** : ~200-300 mA en pic, ~500 mA en moyenne

→ **OK pour LoRa** mais ajouter du decouplage pour les pics TX.

### Decouplage recommande

- **C1** : 22 µF tantale ou ceramique pres du connecteur LoRa
- **C2** : 100 nF ceramique en parallele (haute frequence)
- Optionnel : **ferrite bead** sur la ligne 3.3V pour decoupler la radio du reste

### Alternative : LDO dedie LoRa

Si on veut isoler completement la radio LoRa des perturbations de l'ESP32 :

- **MCP1700-3302E** (250 mA, ultra low quiescent ~1.6 µA)
- **AMS1117-3.3** dedie (800 mA, suffisant)
- **AP2112K-3.3** (600 mA, faible bruit, recommande pour radio)

## Antenne

### Connecteur antenne sur le PCB

- **U.FL** : compact, fragile, necessite un cable pigtail vers l'antenne SMA
- **SMA** (femelle) : robuste, antenne directement vissee, prend plus de place
- **Recommandation** : **U.FL** pour rester compact, avec un pigtail U.FL → SMA si besoin

### Routage RF

- Trace **50Ω impedance controlee** entre le module LoRa et le connecteur antenne
- Distance la plus courte possible
- Plan de masse continu en dessous de la trace
- Vias de couture (stitching vias) tout autour de la trace pour blinder

### Antenne LoRa 868 MHz

- **Antenne fouet 868 MHz SMA** : 3 dBi, ~70 mm, ~5 EUR
- **Antenne PCB 868 MHz** : integree au PCB, plus compact mais perf moindres
- **Antenne helicale** : compromis taille/perf

### Coexistence WiFi 2.4 GHz / LoRa 868 MHz

- Bandes tres eloignees → pas d'interferences directes
- Mais **eloigner physiquement** les deux antennes (>5 cm si possible) pour eviter :
  - Couplage capacitif
  - Harmoniques (3eme harmonique de 868 MHz = 2604 MHz, proche de la WiFi)
- Orientation perpendiculaire des antennes si possible

## Modules LoRa compatibles (sources LCSC)

> Le projet est fabrique chez **JLCPCB**, donc on privilegie les modules disponibles sur **LCSC** pour beneficier du SMT assembly.

### G-NiceRF Lora1276-C1 - **recommande**

- **LCSC** : [C3001532](https://www.lcsc.com/product-detail/C3001532.html)
- **Chip** : Semtech SX1276
- **Frequence** : 868 MHz (EU868)
- **Sensibilite** : -139 dBm
- **Interface** : SPI
- **Footprint** : SMD 16x16 mm
- **Prix** : ~6.44 USD
- **Stock LCSC** : 369 unites (2026-04)
- **Avantages** : module pre-assemble complet (matching network + cristal), quasi pin-compatible RFM95W, librairies tres matures
- **Inconvenients** : stock LCSC limite (~300-400 unites), verifier disponibilite avant prod

### Ai-Thinker Ra-01H - **alternative low-cost**

- **LCSC** : [C503593](https://www.lcsc.com/product-detail/C503593.html)
- **Chip** : Semtech SX1276
- **Frequence** : 868 MHz (EU868) — attention : Ra-01 = 433 MHz, **Ra-01H = 868 MHz**
- **Interface** : SPI
- **Footprint** : SMD-16P
- **Prix** : ~4.38 USD
- **Stock LCSC** : 1477 unites (2026-04)
- **Avantages** : moins cher, **meilleur stock LCSC**, fabricant repandu (Ai-Thinker fait aussi les modules ESP)
- **Inconvenients** : doc moins detaillee que G-NiceRF, certification CE moins claire

### Comparatif rapide

| Critere | G-NiceRF Lora1276-C1 | Ai-Thinker Ra-01H |
|---------|----------------------|--------------------|
| Chip | SX1276 | SX1276 |
| Frequence | 868 MHz | 868 MHz |
| Prix unitaire | 6.44 USD | 4.38 USD |
| **Stock LCSC** | **369** | **1477** |
| Sensibilite | -139 dBm | -139 dBm |
| Footprint | SMD 16x16mm | SMD-16P |
| Doc qualite | Bonne | Moyenne |
| Verdict | Qualite premium | Meilleur rapport prix/dispo |

→ **Pour la production en serie** : choisir le **Ra-01H** (4x plus de stock + 30% moins cher)
→ **Pour un prototype unique** : les deux conviennent

### Modules a eviter (pour ce projet)

- **C80171 (SX1276 puce nue)** : impossible sans expertise RF (matching network, filtre, cristal, blindage, certification)
- **HopeRF RFM95W** : disponibilite irreguliere sur LCSC, prix variable
- **Heltec WiFi LoRa 32** : carte complete ESP32+LoRa, redondant avec notre ESP32 existant
- **Adafruit RFM9x Breakout** : pour prototypage breadboard uniquement, pas pour SMT assembly

## Considerations LoRaWAN

Si l'objectif est de remonter les donnees via **TheThingsNetwork (TTN)** ou un autre operateur LoRaWAN :

### Frequence

- **868 MHz** en Europe (bande EU868)
- 915 MHz en Amerique du Nord
- 433 MHz autorise mais pas LoRaWAN

### Duty cycle EU868

- Limite legal : **1%** sur la plupart des sous-bandes
- Concretement : ~36 secondes de TX par heure max
- Pour ModuleAir : envoi toutes les **5-10 minutes** maximum (au lieu des 60s actuelles en WiFi)

### Payload

- Limite : **51 bytes** au DR0 (SF12), jusqu'a 222 bytes au DR5 (SF7)
- Strategie : payload binaire compact (bit-packing)
  - Exemple compact : `[pm1:1B, pm25:1B, pm10:1B, co2:2B, temp:1B, humi:1B, hcho:1B] = 8 bytes`

### Activation

- **OTAA** (recommande) : DevEUI + AppEUI + AppKey
- **ABP** (deconseille mais possible) : DevAddr + NwkSKey + AppSKey

## Software - Preparer le firmware

### Bibliotheques recommandees

| Lib | Pour | Avantages |
|-----|------|-----------|
| **sandeepmistry/LoRa** | LoRa point-a-point uniquement | Tres simple, pedagogique |
| **RadioLib** | LoRa + LoRaWAN + autres radios | Universel, tres complet, recommande |
| **MCCI LoRaWAN LMIC** | LoRaWAN strict | Standard, lourd, peu lisible |

→ **RadioLib** est le meilleur choix : supporte SX127x, SX126x, LoRaWAN, et reste lisible.

### Structure code proposee

```
src/
  lora_manager.h
  lora_manager.cpp     # Init, send, callbacks
  lora_payload.h       # Encodage binaire compact
  lora_payload.cpp
```

### Definitions a ajouter dans `config.h`

```c
// LoRa (SX1276 / RFM95W via VSPI)
#define LORA_SCK    18
#define LORA_MISO   19
#define LORA_MOSI   23
#define LORA_CS     5
#define LORA_RST    26
#define LORA_DIO0   34
#define LORA_DIO1   35   // Optionnel (LoRaWAN class B/C)

// LoRa region/freq
#define LORA_FREQ_HZ  868100000UL  // EU868
#define LORA_TX_POWER 14            // dBm (max 14 en EU868 sub-band 1)
```

### Coexistence avec le code existant

- **Pas de conflit hardware** : VSPI vs HSPI (matrice)
- **Pas de conflit logiciel** : `LoRa.begin()` n'interfere pas avec le WiFi/BLE
- L'envoi WiFi (toutes les 60s) et LoRa (toutes les 5-10 min) peuvent coexister
- Mode **WiFi only** ou **LoRa only** ou **dual** configurable depuis le dashboard

## Checklist revision PCB

- [ ] Exposer IO5, IO18, IO19, IO23 (VSPI) sur un connecteur dedie
- [ ] Exposer IO26 (RST), IO34 (DIO0), IO35 (DIO1 optionnel)
- [ ] Connecteur JST XH 2.5mm 8P (ou 10P)
- [ ] Pull-up 10k sur IO5 (CS) vers 3.3V
- [ ] Decouplage 22µF + 100nF pres du connecteur LoRa
- [ ] (Optionnel) LDO 3.3V dedie pour la LoRa
- [ ] Connecteur antenne U.FL ou SMA
- [ ] Trace RF 50Ω vers le connecteur antenne
- [ ] Plan de masse continu sous la trace RF
- [ ] Eloignement antenne LoRa / antenne WiFi (>5 cm)
- [ ] Tester avec un module RFM95W ou breakout Adafruit RFM9x
- [ ] Compatibilite ascendante : le PCB doit toujours fonctionner sans LoRa branchee

## Cout estimatif

| Composant | Prix unitaire |
|-----------|---------------|
| Module RFM95W | ~5 EUR |
| Connecteur U.FL PCB | ~0.50 EUR |
| Pigtail U.FL → SMA | ~2 EUR |
| Antenne 868 MHz fouet SMA | ~5 EUR |
| Connecteur JST XH 8P | ~0.30 EUR |
| Composants passifs (R, C) | ~0.20 EUR |
| **Total ajout LoRa** | **~13 EUR** |

## References

- [RFM95W Datasheet](https://www.hoperf.com/data/upload/portal/20190801/RFM95W-V2.0.pdf)
- [Semtech SX1276 Datasheet](https://semtech.my.salesforce.com/sfc/p/E0000000JelG/a/2R0000001Rbr/6EfVZUorrpoKFfvaF_Fkpgp5kzjiNyiAbqcpqh9qSjE)
- [RadioLib Documentation](https://github.com/jgromes/RadioLib)
- [TheThingsNetwork EU868 frequency plan](https://www.thethingsnetwork.org/docs/lorawan/frequency-plans/)
- [LoRaWAN regional parameters](https://lora-alliance.org/resource_hub/rp2-1-0-3-lorawan-regional-parameters/)
