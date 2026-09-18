# Schéma de départ — Régie/Hub (adaptateur Mega + ESP32 + nRF24)

`regie_hub_mega_esp32.kicad_sch` : même méthode que les deux cartes buzzer (généré, **vérifié avec `kicad-cli sch erc` réel**). 11 composants, 14 nets.

## Ce que cette carte est (et n'est pas)

**Ce n'est PAS un shield Mega complet.** Le shield DMX (`quizkicad/mega`, projet séparé, non touché) s'empile déjà directement sur la Mega. Cette carte-ci est un **petit adaptateur** qui porte l'ESP32 + le nRF24 + l'électronique entre les deux, relié à la Mega par seulement **6 fils** :

| Broche J_MEGA | Signal | Vers la Mega |
|---|---|---|
| 1 | TX2 (5V) | Mega pin 16 |
| 2 | RX2 | Mega pin 17 |
| 3 | TX3 (5V) | Mega pin 14 |
| 4 | RX3 | Mega pin 15 |
| 5 | 5V | Sortie régulateur onboard de la Mega |
| 6 | GND | Masse commune |

**Alimentation** : pas de batterie sur cette carte — confirmé par toi : "on branche le mega [secteur] mais le reste des modules sera alimenté [par lui]". Le 5V vient du régulateur déjà présent sur la Mega.

## Pourquoi un pont diviseur et pas un module convertisseur de niveau logique

Tu as mentionné un "convertisseur logique" entre les RX/TX de la Mega et de l'ESP32. J'ai utilisé le **pont diviseur 1kΩ/2kΩ** à la place d'un module dédié — ce n'est pas un raccourci, c'est la solution **déjà documentée et déjà validée sur ce projet** (`cl.md` section 3.3, schéma dans `docs/APK_HYBRIDE_GUIDE.md`) pour exactement ces deux liaisons série. Réutiliser une solution qui marche déjà coûte moins cher et évite d'introduire une dépendance à un nouveau module non testé sur ce projet.

- **R1/R2** : diviseur sur Mega TX2 (5V) → ESP32 RX1 (3,3V) — lien Jeu.
- **R3/R4** : diviseur sur Mega TX3 (5V) → ESP32 RX2 (3,3V) — lien Config EEPROM.
- **Pas de diviseur dans l'autre sens** (ESP32 TX → Mega RX) : déjà documenté que le 3,3V est lu comme HIGH par les entrées 5V de la Mega, connexion directe.

## Pourquoi un régulateur dédié au nRF24 (pas le 3V3 de l'ESP32)

Demandé : "un régulateur pour esp32" + "tout ce qui va avec ces composants pour un meilleur rendement". Plutôt que d'alimenter le nRF24 depuis la broche 3V3 embarquée de l'ESP32 (qui partage déjà la charge du Wi-Fi — creux de tension possible, cause connue d'instabilité nRF24, historique documenté sur ce projet), **U_REG** est un module régulateur 3,3V dédié rien que pour le nRF24, avec son propre découplage (C2, C3 — le condensateur 10-100µF déjà exigé par `cl.md` pour le nRF24).

## Avertissements ERC (36 au total)

Même profil que les deux cartes buzzer, déjà expliqué en détail dans `../kicad_buzzer_nano/README.md` — normal en pratique KiCad (comparé contre `quizkicad/mega`, ton propre projet fait à la main) :

| Type | Nombre | Normal ? |
|---|---|---|
| `endpoint_off_grid` | 20 | Cosmétique |
| `lib_symbol_mismatch` | 9 | Cosmétique — `Outils > Mettre à jour les symboles depuis la bibliothèque` |
| `pin_not_connected` | 4 | 2 attendus (3V3 onboard ESP32 et IRQ nRF24, non utilisés — no-connect posé), 2 liés au réseau GND (nuance ERC normale, voir README buzzer) |
| `no_connect_dangling` | 2 | Marqueurs "pas de connexion" légèrement décalés — à recaler d'un clic |
| `power_pin_not_driven` | 1 | Normal sur un réseau GND (également présent dans `quizkicad/mega`) |

## PCB : placement + sérigraphie + pistes routées

`regie_hub_mega_esp32.kicad_pcb` : empreintes placées, sérigraphie, **et pistes routées** — même pipeline que les deux cartes buzzer (Freerouting en CLI headless + réimport KiCad + `kicad-cli pcb drc` réel), voir `../kicad_buzzer_nano/README.md` pour le détail.

- Chaque composant (J_MEGA, U_ESP32, J_NRF, U_REG, R1-R4, C1-C3) porte son repère + une note sur sa propre sérigraphie (ex. R1 "1k (diviseur TX2, série)").
- Bloc "BROCHAGE" en bas de carte reprenant tout le tableau de brochage ci-dessus, plus un rappel que le shield DMX existant s'empile sur la Mega, pas sur cette carte.
- Aperçu : `apercu_pcb.png`.

**Résultat** : 61 connexions (14 nets), **0 violation de clearance, 0 élément non connecté**, 1 via placée. DRC final : 13 avertissements — 11 `lib_footprint_mismatch` (cosmétique) + 2 `track_dangling` (résidu d'autorouter, à nettoyer avec `Outils > Nettoyer les pistes et les vias`, sans impact électrique).

**Point à vérifier toi-même** : le routage automatique ne sait pas prioriser "au plus près" pour C3 (condensateur de découplage nRF24, `cl.md` demande qu'il soit proche du module) — vérifie à l'œil que la piste C3↔J_NRF reste courte et directe, sinon rapproche C3 de J_NRF et relance juste cette piste à la main.

## Prochaine étape

Ouvre le PCB dans KiCad, nettoie les 2 `track_dangling`, vérifie C3↔J_NRF (voir ci-dessus), puis passe aux fichiers de fabrication (Gerbers) — en particulier bien respecter les recommandations `cl.md` (VCC nRF24 en 3,3V uniquement jamais 5V, GND commun).
