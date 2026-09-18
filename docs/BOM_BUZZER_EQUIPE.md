# BOM & brochage — Boîtiers buzzer équipe (Nano+nRF24 et ESP32)

Référence pour démarrer le schéma KiCad des deux cartes. Basé sur le câblage déjà utilisé par le firmware (`buzzer_nano_equipe.ino` / `esp32_buzzer_equipe.ino`) — **aucun changement de code nécessaire**, seul le matériel autour est nouveau.

---

## Vibreur : module tout fait (IN / VCC / GND)

Confirmé : les vibreurs utilisés sont des **modules** à 3 broches (IN / VCC / GND), pas des moteurs nus. Le transistor de commande, la résistance de base et la diode de roue libre sont déjà intégrés sur le petit PCB du module — pas besoin de les reproduire sur la carte buzzer.

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
| Batterie LiPo 1S 3,7V | Voir capacité par carte ci-dessous | Alimentation |
| Connecteur batterie | JST-PH 2 points | Standard LiPo, polarité gardée |
| Module charge + protection | TP4056 **ou TP4057** (version améliorée, protection inversion de polarité en plus) + DW01A/FS8205A, **variante USB-C** | Charge + coupure basse tension automatique (~2,4-3,0V) + protection surcharge/court-circuit |
| Boost 5V | Module boost LiPo→5V (ex. base MT3608) | Tension stable 5V quelle que soit la charge de la batterie |

⚠️ **Le module charge (TP4056/TP4057) et le boost 5V sont deux fonctions distinctes** — certaines cartes combinées ("3 en 1") intègrent les deux sur le même petit PCB, d'autres non. **Vérifié pour le TP4057 en particulier : c'est une puce de charge pure, sans fonction boost.** Si ton module ne fait que charge+protection, il faut un second module boost séparé. Câblage dans ce cas :

```
Batterie (+/-) ──┬──▶ Module TP4057 (bornes BAT) — charge/protection, en parallèle
                  └──▶ Entrée du module boost — aussi en parallèle
Sortie du module boost (5V) ──▶ J2 broches 3/4 sur le PCB
```

Ça ne change rien au PCB (J2 reste 4 broches : BAT+, BAT-, 5V+, 5V-) — juste la façon dont les modules externes se câblent entre eux avant de rejoindre J2.
| Interrupteur | Glissière ou bouton-poussoir à verrouillage, sur la ligne 5V après le boost | Éteindre complètement entre deux events — sans lui l'autonomie 4-6h ne veut plus rien dire si le boîtier reste allumé par oubli |
| Indicateur niveau batterie | Module "1S lithium battery capacity indicator", 4 LED (25/50/75/100%) | Branché directement en parallèle sur la batterie (2 fils B+/B-, ~5mA, toujours actif, pas de bouton) — repère `BATLED` sur le PCB |

---

## Carte 1 — Nano + nRF24 (équipe, version principale)

### BOM

| Réf. | Composant | Note |
|---|---|---|
| U1 | Arduino Nano (ou compatible CH340) | Monté en carrier via headers femelles — pas besoin de graver un bootloader séparément |
| U2 | Module nRF24L01+ | **VCC en 3,3V uniquement, jamais 5V** |
| C1 | Condensateur 10-100 µF | Entre VCC et GND du nRF24, obligatoire (bruit radio) |
| SW1 | Bouton-poussoir | Buzz |
| M1 | Module vibreur (IN/VCC/GND) | Optionnel — voir câblage simplifié ci-dessus |
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
| SW1 | Bouton-poussoir | Buzz |
| M1 | Module vibreur (IN/VCC/GND) | Optionnel — voir câblage simplifié ci-dessus |
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
- **Condensateurs et résistances** : mix CMS (montés en machine ou à la pince) et traversants (soudés à la main) selon ce qui est disponible/pratique.
- **Batterie** : câblée directement (soudée), logée dans son propre compartiment du boîtier (voir ci-dessous).

## Ce que ça implique pour le boîtier 3D

Boîtier en **deux étages démontables indépendamment** (voir `hardware/boitier_buzzer_equipe.scad`, rendu et vérifié avec OpenSCAD) :
- **Étage du haut** : PCB + connecteurs + modules, fermé par un couvercle avec le trou du bouton BUZZ. C'est là que sont câblés les headers.
- **Étage du bas** : compartiment batterie séparé, avec sa propre trappe — on peut changer/recharger la batterie sans démonter l'électronique du dessus.
- Une étagère interne sépare les deux, avec une fente de passage pour les fils d'alimentation.
- **Carte ESP32** : batterie ~2x plus grosse (1000-1200 mAh vs 500-600 mAh) → empreinte au sol différente entre les deux variantes (52×28mm de carte ESP32 confirmés par la fiche produit, contre la Nano+nRF24 plus compacte).
- Accès externe prévu au port USB-C (charge, sur l'étage batterie) et à l'interrupteur (sur l'étage PCB), sans avoir à ouvrir les couvercles.
- **Module charge+protection+boost 3-en-1** : loge **dans l'étage batterie**, à côté de la cellule (câblage BAT+/BAT- en parallèle sur la batterie, sortie 5V/GND qui remonte par la fente de passage de câbles jusqu'à J2 sur le PCB). L'étage batterie est maintenant dimensionné pour la batterie **et** ce module côte à côte (avant, il n'y avait de la place que pour la batterie seule) — dimensions du module estimées génériquement (26×18×6mm), **à remesurer sur le module réellement reçu** et ajuster `module_charge_l/p/h` dans le `.scad` si besoin.
