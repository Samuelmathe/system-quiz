# BOM & brochage — Adaptateur Régie/Hub (Mega + ESP32 + nRF24)

Référence pour le schéma KiCad `hardware/kicad_regie_hub/regie_hub_mega_esp32.kicad_sch`. Carte **adaptateur**, pas un shield Mega complet — le shield DMX (`quizkicad/mega`, projet séparé) s'empile déjà directement sur la Mega.

## Alimentation

**Pas de batterie sur cette carte.** Le système est alimenté secteur : on branche la Mega (son régulateur onboard convertit l'alimentation secteur en 5V propre), et cette carte adaptateur — ESP32, nRF24, tout le reste — tire son 5V directement de la Mega via le connecteur `J_MEGA`.

## BOM

| Réf. | Composant | Note |
|---|---|---|
| J_MEGA | Connecteur 6 broches (fils vers la Mega) | TX2, RX2, TX3, RX3, 5V, GND — voir tableau ci-dessous |
| U_ESP32 | ESP32 DevKit V1 (module sur connecteur femelle, pas soudé) | Carte confirmée : FT232, 30 broches, micro-USB |
| J_NRF | Module nRF24L01+ (sur connecteur femelle) | **VCC en 3,3V uniquement, jamais 5V** |
| U_REG | Module régulateur 3,3V (ex. AMS1117-3.3) | Dédié au nRF24 — voir raisonnement dans le README du dossier KiCad |
| R1, R3 | Résistance 1 kΩ | Diviseurs de tension (série) |
| R2, R4 | Résistance 2 kΩ | Diviseurs de tension (vers GND) |
| C1, C2 | Condensateur ~10 µF | Découplage entrée/sortie du régulateur |
| C3 | Condensateur 10-100 µF | Découplage nRF24 (obligatoire, cf. `cl.md` section 3.3) |

## Brochage

| Signal | Vers | Note |
|---|---|---|
| J_MEGA.1 (TX2, 5V) | R1 (entrée diviseur) | Lien Jeu, 19200 bauds |
| R1/R2 (nœud diviseur, 3,3V) | U_ESP32 GPIO26 (RX1) | |
| U_ESP32 GPIO25 (TX1) | J_MEGA.2 (RX2) | Direct, pas de diviseur (3,3V lu HIGH par la Mega) |
| J_MEGA.3 (TX3, 5V) | R3 (entrée diviseur) | Lien Config EEPROM, 9600 bauds |
| R3/R4 (nœud diviseur, 3,3V) | U_ESP32 GPIO16 (RX2) | |
| U_ESP32 GPIO17 (TX2) | J_MEGA.4 (RX3) | Direct |
| U_ESP32 GPIO4/5/18/19/23 | J_NRF CE/CSN/SCK/MISO/MOSI | SPI matériel (VSPI) |
| U_REG VOUT (3,3V dédié) | J_NRF VCC | Pas le 3V3 onboard de l'ESP32 — voir README |
| U_ESP32 GPIO2 | — | LED embarquée, interne au module, rien à router ici |

## À commander (spécifications, pas de lien direct — voir explication dans la conversation)

- **ESP32 DevKit V1** : déjà commandé (FT232, 30 broches, micro-USB) pour cette carte.
- **Module nRF24L01+** : version standard, alimentation 3,3V.
- **Module régulateur AMS1117-3.3** : cherche `AMS1117 3.3V module régulateur LDO`.
