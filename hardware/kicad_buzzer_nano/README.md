# Schéma de départ — Buzzer équipe (Nano + nRF24)

`buzzer_equipe_nano.kicad_sch` : schéma généré à partir du BOM (`../../docs/BOM_BUZZER_EQUIPE.md`), **vérifié avec KiCad lui-même** (`kicad-cli sch erc`, format v9.0) — pas juste du texte non testé. 8 composants, 12 nets connectés (alimentation, bouton, module vibreur, nRF24 sur SPI matériel).

## ⚠️ Correction : broches VCC/GND du nRF24 inversées dans une version précédente

En répondant à ta question "le nrf24 c'est sur une ligne ?", j'ai vérifié le brochage du module nRF24 contre 3 sources indépendantes (components101.com, lastminuteengineers.com, doc générale nRF24L01+) et découvert que **J1 avait VCC en broche 1 et GND en broche 2 — c'est l'inverse** de la vraie numérotation (broche 1 = GND, broche 2 = VCC ; la broche 1 est d'ailleurs toujours la pastille carrée sur cette empreinte, et GND est documenté comme "identifiable par son marquage carré" sur les vrais modules). Corrigé partout : schéma, PCB, ce README. Si tu avais déjà commencé à câbler en suivant l'ancienne version, vérifie/corrige avant de mettre sous tension.

Tu as aussi confirmé que ton module est un **nRF24L01+PA+LNA** (longue portée, antenne externe) — ses 8 broches sont sur un connecteur **2×4**, pas 1×8 comme le module standard. L'empreinte J1 a été changée en conséquence (voir section PCB ci-dessous).

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

- Chaque composant porte son repère (A1L/A1R, J1, C1...) et une valeur/note sur son propre texte de sérigraphie.
- Un bloc "BROCHAGE" en bas de la carte reprend toutes les correspondances de broches (ex. `J1 nRF24 PA+LNA (2x4) -- VERIFIER sur le module: 1 GND 2 VCC(3V3) 3 CE 4 CSN...`).
- **Le Nano est représenté par de vrais ports femelles** : deux barrettes `PinSocket_1x15` séparées (A1L = broches réelles 1-15, A1R = 16-30, espacées de 15,24mm comme sur un vrai Nano) — pas l'empreinte "Nano soudé directement" d'avant. Le Nano s'enfiche dessus, rien à souder sur le module lui-même. Numérotation vérifiée broche par broche contre la vraie empreinte `Module:Arduino_Nano` (broche 16 en bas, broche 30 en haut de la colonne droite).
- **J1 (nRF24) est maintenant un connecteur femelle 2×4** (`PinSocket_2x04`), pour le module PA+LNA que tu as confirmé. Broches 1=GND et 2=VCC réparties sur la 1ère rangée, puis CE/CSN, SCK/MOSI, MISO/IRQ sur les 3 rangées suivantes — c'est l'ordre le plus souvent publié pour ce type de module, mais **aucun standard universel n'existe pour les variantes PA+LNA** (contrairement au module simple 1×8). ⚠️ **Vérifie les repères imprimés sur ton module avant de souder** — s'ils ne correspondent pas à l'ordre ci-dessus, il suffit de recâbler les 8 fils, pas de refaire la carte.
- Aperçu : `apercu_pcb.png` (rendu réel via `kicad-cli pcb render` — les pistes visibles sur ce rendu sont uniquement celles de la face du dessus, les pistes côté B.Cu n'apparaissent pas sur cette vue).

**Pipeline utilisé** : `pcbnew.ExportSpecctraDSN()` → `freerouting.jar` (mode CLI headless, `-mp 20`) → `pcbnew.ImportSpecctraSES()` → `kicad-cli pcb drc`. 59 connexions, **0 violation de clearance, 0 élément non connecté**.

**DRC final** : 9 avertissements, tous `lib_footprint_mismatch` (cosmétique, se corrige avec `Outils > Mettre à jour les empreintes depuis la bibliothèque`). Les résidus d'autorouter (`track_dangling`, tronçons en double qui ne menaient à aucune pastille) ont été identifiés et supprimés via script `pcbnew` en vérifiant d'abord la topologie réelle (ne pas casser une piste utile), puis revérifiés avec `kicad-cli pcb drc` — **0 track_dangling restant**.

**À vérifier toi-même avant fabrication** : largeur de piste/clearance par défaut de KiCad (pas de netclass personnalisée définie ici) — correcte pour du signal logique, mais à confirmer sur les nets d'alimentation (`VCC_5V_SW`, `GND`) selon le courant réel du vibreur.

## Prochaine étape

Ouvre `buzzer_equipe_nano.kicad_pcb` dans KiCad et relis le routage à l'œil (largeurs de piste, trajets) avant de passer aux fichiers de fabrication (Gerbers).

La version ESP32 (secours équipe) est dans `../kicad_buzzer_esp32/`, même méthode.
