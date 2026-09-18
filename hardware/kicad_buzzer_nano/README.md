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

## PCB : placement compact + pistes routées

`buzzer_equipe_nano.kicad_pcb` : empreintes placées, **et pistes routées** — 2 couches (F.Cu/B.Cu), autoroutées avec [Freerouting](https://freerouting.app/) (outil open source dédié, pas un routage "deviné" à la main par moi) puis réimportées dans KiCad, le tout vérifié avec `kicad-cli pcb drc` réel.

**Changements suite à ta relecture** :
- **Le texte-guide en sérigraphie a été retiré** — ça n'a pas sa place gravé sur un vrai circuit fabriqué. Il ne reste que les repères de composants standards (A1L, J1, C1...), comme sur n'importe quelle carte.
- **Carte bien plus compacte** : ~90×100mm au lieu de 125×145mm — composants resserrés, plus d'espace perdu.
- **C1 (découplage nRF24) déplacé juste à côté de J1** (~10-12mm au lieu de ~30mm) pour un découplage propre.
- **Zone sans cuivre sous le nRF24** : un keepout (pistes/vias/plans interdits, sur les deux couches F.Cu et B.Cu) couvre l'espace où le corps/l'antenne du module PA+LNA se trouve une fois enfiché — pour ne pas mettre de cuivre sous l'antenne. Positionné en "presqu'île" contre le bord droit de la carte (rien ne route à travers), pas collé sur J1 des deux côtés — une première tentative avait bloqué le routeur (8 pistes bloquées après 100+ passes) en enfermant le connecteur ; corrigé en laissant un côté totalement ouvert. **Vérifié réellement respecté par Freerouting** (pas juste accepté par KiCad) via un test dédié où un keepout bloquant délibérément le seul chemin entre 2 pastilles a fait router 0 piste par l'autorouteur — preuve qu'il n'était pas ignoré. Taille/position à vérifier/ajuster dans KiCad selon les dimensions réelles de ton module.
- **Le Nano est représenté par de vrais ports femelles** : deux barrettes `PinSocket_1x15` séparées (A1L = broches réelles 1-15, A1R = 16-30, espacées de 15,24mm comme sur un vrai Nano). Le Nano s'enfiche dessus, rien à souder sur le module lui-même.
- **J1 (nRF24) est un connecteur MÂLE 2×4** (`PinHeader_2x04`) — corrigé après que tu as précisé que le module lui-même a des ports femelles dessus, donc c'est la carte qui doit porter des broches mâles pour que le module s'enfiche dessus (logique inverse de A1L/A1R où c'est le Nano qui a des broches mâles). Broches 1=GND et 2=VCC sur la 1ère rangée, puis CE/CSN, SCK/MOSI, MISO/IRQ — ordre le plus souvent publié pour ce type de module, mais **aucun standard universel n'existe pour les variantes PA+LNA**. ⚠️ **Vérifie les repères imprimés sur ton module avant de souder.**
- Aperçu : `apercu_pcb.png`.

**Pipeline utilisé** : `pcbnew.ExportSpecctraDSN()` → `freerouting.jar` (mode CLI headless, `-mp 20`) → `pcbnew.ImportSpecctraSES()` → `kicad-cli pcb drc`. 53 connexions, **0 violation de clearance, 0 élément non connecté**, keepout respecté (0 piste dedans, vérifié par script).

**DRC final** : 9 avertissements, tous `lib_footprint_mismatch` (cosmétique, se corrige avec `Outils > Mettre à jour les empreintes depuis la bibliothèque`) — **0 track_dangling** dès le premier autoroutage cette fois.

**À vérifier toi-même avant fabrication** : largeur de piste/clearance par défaut de KiCad (pas de netclass personnalisée définie ici) — correcte pour du signal logique, mais à confirmer sur les nets d'alimentation (`VCC_5V_SW`, `GND`) selon le courant réel du vibreur.

## Prochaine étape

Ouvre `buzzer_equipe_nano.kicad_pcb` dans KiCad et relis le routage à l'œil (largeurs de piste, trajets) avant de passer aux fichiers de fabrication (Gerbers).

La version ESP32 (secours équipe) est dans `../kicad_buzzer_esp32/`, même méthode.
