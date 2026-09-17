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
| Module charge + protection | TP4056 + DW01A/FS8205A, **variante USB-C** | Charge + coupure basse tension automatique (~2,4-3,0V) + protection surcharge/court-circuit |
| Boost 5V | Module boost LiPo→5V (ex. base MT3608) — souvent intégré au module TP4056 ci-dessus en version combinée | Tension stable 5V quelle que soit la charge de la batterie |
| Interrupteur | Glissière ou bouton-poussoir à verrouillage, sur la ligne 5V après le boost | Éteindre complètement entre deux events — sans lui l'autonomie 4-6h ne veut plus rien dire si le boîtier reste allumé par oubli |

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

## Ce que ça implique pour le boîtier 3D

- **Carte ESP32** : batterie ~2x plus grosse (1000-1200 mAh vs 500-600 mAh) → boîtier plus épais/large sur cette variante. Les deux boîtiers ne pourront pas avoir exactement le même gabarit si on garde ce dimensionnement.
- Prévoir un accès externe au port USB-C (charge) et à l'interrupteur sur la coque, sans avoir à l'ouvrir.
