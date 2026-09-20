# BOM & brochage — Boîtiers buzzer équipe (Nano+nRF24 et ESP32)

Référence pour démarrer le schéma KiCad des deux cartes. Basé sur le câblage déjà utilisé par le firmware (`buzzer_nano_equipe.ino` / `esp32_buzzer_equipe.ino`) — **aucun changement de code nécessaire**, seul le matériel autour est nouveau.

---

## ⚠️ Règle d'achat obligatoire : connecteur pré-câblé sur CHAQUE module

Les cartes PCBA sont livrées **directement au client**, pas à Samuel — il n'y a aucune étape intermédiaire où quelqu'un peut sertir/souder un connecteur avant l'envoi. Résultat : **le client ne doit jamais avoir besoin d'un fer à souder**, donc chaque module externe (pas juste la batterie) doit être acheté dans sa version **déjà équipée d'une prise compatible** sur ses fils, jamais en fils nus.

Chaque ligne concernée ci-dessous est marquée **🔌 connecteur pré-câblé obligatoire à l'achat**. Si un fournisseur ne propose vraiment aucune version pré-câblée pour l'un de ces éléments, c'est le seul point qui devra être signalé explicitement au client comme exception (fer à souder nécessaire) — à éviter autant que possible.

---

## Vibreur : module tout fait (IN / VCC / GND)

Confirmé : les vibreurs utilisés sont des **modules** à 3 broches (IN / VCC / GND), pas des moteurs nus. Le transistor de commande, la résistance de base et la diode de roue libre sont déjà intégrés sur le petit PCB du module — pas besoin de les reproduire sur la carte buzzer.

🔌 **Connecteur pré-câblé obligatoire à l'achat** — chercher une version du module avec un connecteur (JST/Dupont) déjà serti sur les 3 fils IN/VCC/GND, pas des fils nus à souder. Moins garanti que pour une batterie LiPo (module bon marché, souvent vendu en fils nus) — vérifier la fiche produit avant d'acheter, sinon prévoir de sertir soi-même avant tout envoi au client.

Câblage simplifié (identique sur les deux cartes) :

```
   GPIO (moteurPin) ──▶ IN
   5V (sortie boost) ──▶ VCC
   GND                ──▶ GND
```

`digitalWrite(moteurPin, HIGH/LOW)` pilote directement l'entrée IN du module, qui fait la commutation en interne. Si jamais vous passez un jour à un moteur nu (sans module), remettre un étage transistor+résistance+diode comme d'habitude sur ce type de charge inductive — mais ce n'est pas le cas ici.

---

## Bloc alimentation (commun aux deux cartes)

| Élément | Réf. type | Rôle |
|---|---|---|
| Batterie LiPo 1S 3,7V | Voir capacité par carte ci-dessous — 🔌 **connecteur JST-PH pré-serti obligatoire à l'achat** (standard sur la quasi-totalité des batteries hobby, filtrer sur ce critère) | Alimentation |
| Connecteur batterie | JST-PH 2 points, déjà présent côté PCB (BT1, soudé en usine PCBA) | Standard LiPo, polarité gardée |
| Module charge + protection | TP4056 **ou TP4057** (version améliorée, protection inversion de polarité en plus) + DW01A/FS8205A, **variante USB-C** — 🔌 connecteur pré-câblé à privilégier si disponible, sinon exception acceptée (module vendu en carte nue avec pastilles à souder, cas fréquent) | Charge + coupure basse tension automatique (~2,4-3,0V) + protection surcharge/court-circuit |
| Boost 5V | Module boost LiPo→5V (ex. base MT3608) — même remarque que ci-dessus, souvent vendu en carte nue | Tension stable 5V quelle que soit la charge de la batterie |

⚠️ **Le module charge (TP4056/TP4057) et le boost 5V sont deux fonctions distinctes** — certaines cartes combinées ("3 en 1") intègrent les deux sur le même petit PCB, d'autres non. **Vérifié pour le TP4057 en particulier : c'est une puce de charge pure, sans fonction boost.** Si ton module ne fait que charge+protection, il faut un second module boost séparé. Câblage dans ce cas :

```
Batterie (+/-) ──┬──▶ Module TP4057 (bornes BAT) — charge/protection, en parallèle
                  └──▶ Entrée du module boost — aussi en parallèle
Sortie du module boost (5V) ──▶ J2 broches 3/4 sur le PCB
```

Ça ne change rien au PCB (J2 reste 4 broches : BAT+, BAT-, 5V+, 5V-) — juste la façon dont les modules externes se câblent entre eux avant de rejoindre J2.
| Interrupteur | Glissière ou bouton-poussoir à verrouillage, sur la ligne 5V après le boost — 🔌 connecteur pré-câblé obligatoire à l'achat (fils avec Dupont/JST déjà sertis, pas de cosses nues) | Éteindre complètement entre deux events — sans lui l'autonomie 4-6h ne veut plus rien dire si le boîtier reste allumé par oubli |
| Indicateur niveau batterie | Module "1S lithium battery capacity indicator", 4 LED (25/50/75/100%) — 🔌 connecteur pré-câblé obligatoire à l'achat | Branché directement en parallèle sur la batterie (2 fils B+/B-, ~5mA, toujours actif, pas de bouton) — repère `BATLED` sur le PCB |

---

## Carte 1 — Nano + nRF24 (équipe, version principale)

### BOM

| Réf. | Composant | Note |
|---|---|---|
| U1 | Arduino Nano (ou compatible CH340) | Monté en carrier via headers femelles — pas besoin de graver un bootloader séparément |
| U2 | Module nRF24L01+ | **VCC en 3,3V uniquement, jamais 5V** |
| C1 | Condensateur 10-100 µF | Entre VCC et GND du nRF24, obligatoire (bruit radio) |
| SW1 | Bouton-poussoir — 🔌 connecteur pré-câblé obligatoire à l'achat | Buzz |
| M1 | Module vibreur (IN/VCC/GND) — 🔌 connecteur pré-câblé obligatoire à l'achat (voir remarque en début de doc) | Optionnel — voir câblage simplifié ci-dessus |
| — | Bloc alimentation | Voir section commune — **capacité batterie recommandée : 500-600 mAh** (consommation Nano+nRF24 plus faible, ~20-25 mA moyen estimé) |

### Brochage (Nano)

| Signal | Broche Nano | Vers |
|---|---|---|
| nRF24 CE | D9 | Module nRF24 |
| nRF24 CSN | D10 | Module nRF24 |
| nRF24 SCK | D13 | Module nRF24 *(partagée avec LED_BUILTIN — sans incidence, le firmware n'allume la LED que hors transaction SPI)* |
| nRF24 MOSI | D11 | Module nRF24 |
| nRF24 MISO | D12 | Module nRF24 |
| nRF24 VCC | 3,3V (broche Nano) | Module nRF24 |
| nRF24 GND | GND | Module nRF24 |
| Bouton | D2 → GND | Pull-up interne activé par le firmware, pas de résistance externe |
| Vibreur — IN | D5 | Module vibreur |
| Vibreur — VCC | 5V | Sortie du boost |
| Vibreur — GND | GND | — |
| Alimentation | 5V / GND | Sortie du boost |

---

## Carte 2 — ESP32 (secours équipe, ESP-NOW)

### BOM

| Réf. | Composant | Note |
|---|---|---|
| U1 | ESP32 Dev Module | Pas de module radio externe (Wi-Fi/ESP-NOW intégré à la puce) — carte envisagée : ESP32-DevKitC-32, USB-C, **CH340C** (pas encore commandée). Pilote **CH340**, différent du FT232 de la carte hub régie — ne pas confondre au moment d'installer les pilotes. |
| SW1 | Bouton-poussoir — 🔌 connecteur pré-câblé obligatoire à l'achat | Buzz |
| M1 | Module vibreur (IN/VCC/GND) — 🔌 connecteur pré-câblé obligatoire à l'achat (voir remarque en début de doc) | Optionnel — voir câblage simplifié ci-dessus |
| — | Bloc alimentation | Voir section commune — **capacité batterie recommandée : 1000-1200 mAh** (Wi-Fi nettement plus gourmand, ~60-120 mA moyen estimé, ~2x la carte Nano pour la même autonomie) |

### Brochage (ESP32)

| Signal | Broche ESP32 | Vers |
|---|---|---|
| Bouton | GPIO4 → GND | Pull-up interne activé par le firmware |
| LED (feedback + confirmation ESP-NOW) | GPIO2 | LED embarquée sur la plupart des cartes ESP32 Dev Module |
| Vibreur — IN | GPIO13 | Module vibreur |
| Vibreur — VCC | 5V | Sortie du boost |
| Vibreur — GND | GND | — |
| Alimentation | 5V / GND (broche VIN ou 5V selon la carte) | Sortie du boost |

**Rappel important** : `ESPNOW_WIFI_CHANNEL` (canal 6 par défaut) dans `esp32_buzzer_equipe.ino` doit rester identique à celui du hub (`esp32/esp32_bridge_server.ino`) — sans lien avec le PCB, mais à ne pas oublier au premier flash de chaque carte.

---

## Assemblage : connecteurs, pas de soudure directe

- **Nano, nRF24, et le module ESP32** se montent sur des **connecteurs/headers femelles** soudés sur le PCB — pas soudés directement. Facilite le remplacement en cas de panne d'un module.
- **Condensateurs, résistances, et tous les headers femelles/mâles/JST** : assemblage machine (PCBA) chez le fabricant — rien à souder côté carte pour le client.
- **Batterie et tous les modules externes** (vibreur, bouton, interrupteur, charge/boost, indicateur niveau) : câblage par connecteur à l'achat, **pas de soudure** — voir la règle d'achat en tout début de document. Les cartes étant livrées directement au client (pas de passage par Samuel), chaque module doit arriver déjà équipé de son connecteur, sinon le client se retrouve avec un fer à souder à sortir malgré tout le travail fait sur le PCB.

## Ce que ça implique pour le boîtier 3D

Boîtier en **deux étages démontables indépendamment** (voir `hardware/boitier_buzzer_equipe.scad`, rendu et vérifié avec OpenSCAD) :
- **Étage du haut** : PCB + connecteurs + modules, fermé par un couvercle avec le trou du bouton BUZZ. C'est là que sont câblés les headers.
- **Étage du bas** : compartiment batterie séparé, avec sa propre trappe — on peut changer/recharger la batterie sans démonter l'électronique du dessus.
- Une étagère interne sépare les deux, avec une fente de passage pour les fils d'alimentation.
- **Carte ESP32** : batterie ~2x plus grosse (1000-1200 mAh vs 500-600 mAh) → empreinte au sol différente entre les deux variantes (52×28mm de carte ESP32 confirmés par la fiche produit, contre la Nano+nRF24 plus compacte).
- Accès externe prévu au port USB-C (charge, sur l'étage batterie) et à l'interrupteur (sur l'étage PCB), sans avoir à ouvrir les couvercles.
- **Module charge+protection+boost 3-en-1** : loge **dans l'étage batterie**, à côté de la cellule (câblage BAT+/BAT- en parallèle sur la batterie, sortie 5V/GND qui remonte par la fente de passage de câbles jusqu'à J2 sur le PCB). L'étage batterie est maintenant dimensionné pour la batterie **et** ce module côte à côte (avant, il n'y avait de la place que pour la batterie seule) — dimensions du module estimées génériquement (26×18×6mm), **à remesurer sur le module réellement reçu** et ajuster `module_charge_l/p/h` dans le `.scad` si besoin.
