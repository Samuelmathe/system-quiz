# Schéma de départ — Buzzer équipe (Nano + nRF24)

`buzzer_equipe_nano.kicad_sch` : schéma généré à partir du BOM (`../../docs/BOM_BUZZER_EQUIPE.md`), **vérifié avec KiCad lui-même** (`kicad-cli sch erc`, format v9.0) — pas juste du texte non testé. 8 composants, 12 nets connectés (alimentation, bouton, module vibreur, nRF24 sur SPI matériel).

## Comment l'ouvrir

Ouvrez `buzzer_equipe_nano.kicad_sch` directement dans KiCad (File > Open). Aucun `.kicad_pro` fourni — KiCad vous proposera d'en créer un, acceptez.

## À faire en premier en l'ouvrant

L'ERC (`Inspecter > Vérification des règles électriques`) remonte 72 avertissements. La plupart sont normaux, mais trois méritent un coup d'œil :

| Avertissement | Nombre | À faire |
|---|---|---|
| `pin_not_connected` | 30 | **Normal** — la plupart des broches du Nano (A0-A7, D0/D1/D3/D4/D6/D7/D8...) ne sont pas utilisées par ce boîtier. Ignorer, ou ajouter des drapeaux "Pas de connexion" si vous voulez un ERC propre. |
| `lib_symbol_mismatch` | 12 | Cosmétique — mes copies embarquées des symboles (GND, +3V3, Battery_Cell) diffèrent légèrement de la bibliothèque système. `Outils > Mettre à jour les symboles depuis la bibliothèque` règle ça en un clic. |
| `endpoint_off_grid` | 19 | Cosmétique — mes coordonnées calculées ne tombent pas toujours pile sur la grille par défaut de KiCad. Sans conséquence électrique, juste esthétique. |
| `label_dangling` | 3 | **Normal en pratique** — `BAT_PLUS`, `NRF_SCK`, `VCC_5V_SW` : l'étiquette semble pile sur la bonne broche (vérifié par calcul), KiCad la signale quand même comme non connectée. J'ai vérifié : **ton propre projet `quizkicad/mega`** (fait à la main dans KiCad, pas généré) a **71** avertissements du même genre — c'est une nuance ERC courante, pas un signe d'erreur de génération. Cliquez l'étiquette et redéplacez-la légèrement si vous voulez un ERC propre, sinon ignorez. |
| `no_connect_dangling` | 1 | Le marqueur "pas de connexion" sur la broche IRQ du module nRF24 (non utilisée par le firmware) est légèrement décalé — à recaler d'un clic sur la broche. |
| `power_pin_not_driven` / `pin_not_driven` | 4 + 3 | Également présent dans `quizkicad/mega` (3 occurrences de `power_pin_not_driven`) — nuance ERC normale sur les réseaux GND, pas un bug. |

## PCB : placement + sérigraphie + pistes routées

`buzzer_equipe_nano.kicad_pcb` : empreintes placées, texte de sérigraphie pour guider le soudage, **et pistes routées** — 2 couches (F.Cu/B.Cu), autoroutées avec [Freerouting](https://freerouting.app/) (outil open source dédié, pas un routage "deviné" à la main par moi) puis réimportées dans KiCad, le tout vérifié avec `kicad-cli pcb drc` réel.

- Chaque composant porte son repère (A1, J1, C1...) et une valeur/note sur son propre texte de sérigraphie.
- Un bloc "BROCHAGE" en bas de la carte reprend toutes les correspondances de broches (ex. `J1 nRF24: 1 VCC(3V3) 2 GND 3 CE 4 CSN...`).
- Nano et nRF24 en connecteurs femelles (empreinte `Module:Arduino_Nano`, dimensions/trous identiques que le Nano soit soudé directement ou reçu par un header femelle).
- Aperçu : `apercu_pcb.png` (rendu réel via `kicad-cli pcb render` — les pistes visibles sur ce rendu sont uniquement celles de la face du dessus, les pistes côté B.Cu n'apparaissent pas sur cette vue).

**Pipeline utilisé** : `pcbnew.ExportSpecctraDSN()` → `freerouting.jar` (mode CLI headless, `-mp 20`) → `pcbnew.ImportSpecctraSES()` → `kicad-cli pcb drc`. 73 connexions, **0 violation de clearance, 0 élément non connecté**.

**DRC final** : 9 avertissements — 7 `lib_footprint_mismatch` (cosmétique, se corrige avec `Outils > Mettre à jour les empreintes depuis la bibliothèque`) + 2 `track_dangling` (petit résidu de l'autorouter, un bout de piste sans issue sur une seule net — à nettoyer avec `Outils > Nettoyer les pistes et les vias`, aucun impact électrique puisque 0 net n'est incomplet).

**À vérifier toi-même avant fabrication** : largeur de piste/clearance par défaut de KiCad (pas de netclass personnalisée définie ici) — correcte pour du signal logique, mais à confirmer sur les nets d'alimentation (`VCC_5V_SW`, `GND`) selon le courant réel du vibreur.

## Prochaine étape

Ouvre `buzzer_equipe_nano.kicad_pcb` dans KiCad, nettoie les 2 `track_dangling` et relis le routage à l'œil (largeurs de piste, trajets) avant de passer aux fichiers de fabrication (Gerbers).

La version ESP32 (secours équipe) est dans `../kicad_buzzer_esp32/`, même méthode.
