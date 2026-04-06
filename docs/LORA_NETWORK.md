# LoRaWAN - Strategie d'envoi et reseaux

Analyse de la frequence d'envoi LoRaWAN realiste pour le ModuleAir V4 selon les contraintes des differents reseaux (TTN, Helium, Orange) et la duty cycle EU868.

## Payload ModuleAir compact

Encodage binaire compact des donnees capteur :

| Mesure | Bytes | Encodage |
|--------|-------|----------|
| PM1 | 1 | 0-255 µg/m³ |
| PM2.5 | 1 | 0-255 µg/m³ |
| PM10 | 1 | 0-255 µg/m³ |
| CO2 | 2 | 0-65535 ppm |
| Temperature | 1 | offset signe (-40 a +85 °C) |
| Humidite | 1 | 0-100 % |
| Pression | 2 | offset depuis 900 hPa, ×10 |
| TVOC | 2 | 0-65535 ppb |
| eCO2 | 2 | 0-65535 ppm |
| HCHO | 1 | 0-255 ppb |
| Status / RSSI / flags | 1 | bits packes |
| **Total** | **~15 B** | |

→ Largement en dessous de la limite LoRaWAN a n'importe quel SF (51 B mini en SF12).

## Air time pour 15 bytes selon le Spreading Factor

| SF | Data Rate | Air time (ToA) | Portee typique |
|----|-----------|----------------|----------------|
| SF7 | DR5 | ~46 ms | 2 km urbain |
| SF8 | DR4 | ~82 ms | 4 km |
| SF9 | DR3 | ~165 ms | 6 km |
| SF10 | DR2 | ~330 ms | 8 km |
| SF11 | DR1 | ~660 ms | 12 km |
| SF12 | DR0 | ~1.3 s | 15+ km |

L'**ADR** (Adaptive Data Rate) ajuste automatiquement le SF selon la qualite du lien. En interieur urbain, on est generalement en SF7-SF9.

## Limites legales EU868

**Duty cycle 1%** sur la plupart des sous-bandes : maximum **36 secondes d'air time par heure** sur chaque sous-bande.

Pour 15 bytes :

| SF | Frames max/heure (legal) |
|----|--------------------------|
| SF7 | ~780 (1 toutes les 5 s) |
| SF9 | ~218 (1 toutes les 16 s) |
| SF12 | ~27 (1 toutes les 2 min) |

C'est la **limite legale**. Les reseaux imposent generalement des limites plus strictes.

## Limites par reseau

### TheThingsNetwork (TTN) - Fair Use Policy

**Contrainte la plus stricte des trois reseaux** : TTN impose **30 secondes d'air time par jour par device** (uplink), bien plus restrictif que la loi EU868. Plus **10 downlinks par device par jour**.

| SF utilise | Frames max/jour | Intervalle min |
|------------|-----------------|----------------|
| SF7 | ~650 | ~2 min |
| SF8 | ~365 | ~4 min |
| SF9 | ~180 | ~8 min |
| SF10 | ~90 | ~16 min |
| SF11 | ~45 | ~32 min |
| SF12 | ~23 | **~62 min** |

**Recommandation TTN** : viser **10 minutes** d'intervalle. Garantit le respect du fair use meme si l'ADR descend a SF11/SF12 occasionnellement.

**Cout** : gratuit (community network), sponsorise par The Things Industries.
**Couverture** : variable, depend des passerelles communautaires. Bien couverte dans les grandes villes europeennes, eparse en zone rurale.

### Helium Network

Pas de fair use policy. Modele economique pay-per-packet :

- **1 Data Credit (DC) = 24 bytes**
- **1 USD = 100 000 DC**
- 1 frame de 15 B = 1 DC = **0.00001 USD**

| Intervalle | Frames/jour | Cout/jour | Cout/an |
|------------|-------------|-----------|---------|
| 1 min | 1440 | $0.0144 | ~$5 |
| 5 min | 288 | $0.0029 | ~$1 |
| 10 min | 144 | $0.0014 | ~$0.50 |

**Recommandation Helium** : **5 minutes** est confortable. Cout negligeable meme en flotte.

**Attention** : couverture Helium tres inegale en France. Bien verifier la carte ([explorer.helium.com](https://explorer.helium.com)) avant de deployer.

### Orange Live Objects (operateur francais)

Reseau LoRaWAN national operateur. Plans payants :

- **Plan basique** : ~144 messages/jour (~10 min)
- **Plan superieur** : ~288 messages/jour (~5 min)
- Tarif : ~1-3 EUR/mois par device selon plan

Pas de fair use restrictif au-dela de la limite legale 1% EU868.

**Recommandation Orange** : **10 minutes** correspond au plan de base, **5 minutes** au plan superieur.

**Avantages** : couverture nationale Orange, support contractuel, SLA, interface professionnelle.
**Inconvenients** : payant par device, configuration moins flexible que TTN.

## Recommandation pour ModuleAir

L'air interieur/exterieur ne change pas seconde par seconde. Un intervalle de **10 minutes** est largement suffisant pour suivre la qualite de l'air, et c'est le sweet spot qui marche sur **les 3 reseaux** sans contrainte.

### Strategie principale

```
Mode normal : 1 frame toutes les 10 minutes
              ~144 frames/jour
              ~15 bytes par frame
```

### Mode alerte (optionnel)

Envoi evenementiel sur depassement de seuil :

```
Si CO2 > seuil_co2_alerte
   OU PM2.5 > seuil_pm25_alerte  
   OU HCHO > seuil_hcho_alerte :
       → envoyer 1 frame d'alerte (port LoRaWAN different)
       → cooldown 5 min pour eviter le spam
```

Les frames d'alerte peuvent utiliser un **port LoRaWAN dedie** (port 2 par exemple) pour permettre au backend de les distinguer des frames de routine.

## Comparaison avec l'envoi WiFi actuel

Le firmware envoie actuellement les donnees toutes les **60 secondes** sur WiFi (1440 frames/jour). **Impossible en LoRa** : c'est ~14x trop frequent pour TTN.

### Mode dual WiFi + LoRa

Si on garde les deux radios :

| Radio | Intervalle | Frames/jour |
|-------|-----------|-------------|
| WiFi | 60 s (actuel) | 1440 |
| LoRa | 10 min | 144 |

Le LoRa devient un **fallback** ou un **canal complementaire** pour les zones sans WiFi.

### Mode LoRa seul

Pour un capteur nomade/exterieur sans WiFi :

| Radio | Intervalle | Frames/jour |
|-------|-----------|-------------|
| LoRa | 10 min | 144 |

Configuration possible depuis le dashboard : `WiFi only` / `LoRa only` / `Dual`.

## Best practices LoRaWAN

### 1. ADR active
Essentiel : laisse le serveur LoRaWAN ajuster automatiquement le SF et la puissance TX selon la qualite du lien. Sans ADR, on consomme inutilement de la batterie et du temps d'air.

### 2. Eviter les confirmed uplinks
Les frames "confirmed" attendent un ACK du serveur. Inconvenients :
- Consomme du downlink (limite a 10/jour sur TTN)
- Ralentit l'envoi suivant
- N'augmente pas significativement la fiabilite (LoRa a deja du FEC)

→ **Utiliser uniquement des unconfirmed uplinks** pour les donnees de routine.

### 3. Class A
Class A (par defaut) est suffisant pour ce use case :
- Le device dort entre les uplinks (faible conso)
- Apres chaque uplink, 2 fenetres de reception courtes pour les downlinks eventuels
- Pas besoin de Class B (synchro beacon) ni Class C (RX permanent, gourmand)

### 4. TX power
Commencer a **+14 dBm** (max legal EU868 sub-band 1). L'ADR ajustera a la baisse si le signal est bon.

### 5. Resynchronisation horloge
Si besoin d'horodatage precis, utiliser le **DeviceTimeReq MAC command** (downlink) plutot qu'un NTP via WiFi. Une fois par jour suffit.

### 6. Compression
Le payload de 15 B est deja compact. Si on doit reduire encore :
- Utiliser des deltas (envoyer la difference avec la frame precedente)
- Bit-packing fin (ex : 12 bits pour CO2 jusqu'a 4095 ppm)
- Mais souvent inutile, 15 B passe partout

## Tableau recapitulatif

| Reseau | Intervalle recommande | Frames/jour | Cout estimatif |
|--------|----------------------|-------------|----------------|
| **TTN** | 10 min | 144 | Gratuit |
| **Helium** | 5 min (si couverture) | 288 | ~0.50 USD/an |
| **Orange** | 10 min | 144 | ~12-36 EUR/an |

**Pour rester compatible avec les 3 reseaux : 10 minutes est le bon compromis.**

C'est aussi le standard de fait pour les capteurs environnementaux LoRa (sondes CO2, particules, meteo, etc.).

## References

- [TTN Fair Use Policy](https://www.thethingsnetwork.org/docs/lorawan/duty-cycle/)
- [LoRaWAN EU868 Regional Parameters](https://lora-alliance.org/resource_hub/rp2-1-0-3-lorawan-regional-parameters/)
- [Helium Data Credits](https://docs.helium.com/iot/data-credits/)
- [Orange Live Objects](https://liveobjects.orange-business.com/)
- [LoRa Air Time Calculator](https://avbentem.github.io/airtime-calculator/ttn/eu868)
