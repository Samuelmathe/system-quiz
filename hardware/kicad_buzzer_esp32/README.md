# Schéma de départ — Buzzer équipe (secours ESP32 / ESP-NOW)

`buzzer_equipe_esp32.kicad_sch` : même méthode que `../kicad_buzzer_nano/` (généré à partir du BOM, vérifié avec `kicad-cli sch erc` réel). 6 composants, 6 nets.

## L'ESP32 est représenté comme un connecteur générique 4 broches

Il n'existe pas de symbole KiCad standard pour la carte exacte commandée (ESP32 DevKit V1/ESP32-DevKitC-32 — trop de variantes de brochage différentes selon les fabricants). **U1** représente donc uniquement les 4 signaux réellement câblés sur ce PCB vers le module ESP32, pas les ~30 broches physiques :

| Broche U1 | Signal ESP32 | Usage (voir `esp32_buzzer_equipe.ino`) |
|---|---|---|
| 1 | GPIO4 | Bouton buzz |
| 2 | GPIO13 | IN du module vibreur |
| 3 | 5V / VIN | Alimentation |
| 4 | GND | Masse |

GPIO2 (LED embarquée) n'apparaît pas : elle est **interne au module ESP32 lui-même**, rien à router sur ce PCB pour elle.

## Avertissements ERC (28 au total)

| Type | Nombre | Normal ? |
|---|---|---|
| `endpoint_off_grid` | 12 | Cosmétique, sans conséquence électrique |
| `pin_not_connected` | 7 | À vérifier une fois dans l'interface — voir note ci-dessous |
| `lib_symbol_mismatch` | 7 | Cosmétique — `Outils > Mettre à jour les symboles depuis la bibliothèque` |
| `label_dangling` | 1 | **Normal en pratique** — ton propre projet `quizkicad/mega` (fait à la main) en a **71** du même genre. Pas un signe d'erreur de génération. |
| `power_pin_not_driven` | 1 | **Normal en pratique** — présent aussi dans `quizkicad/mega` (3 occurrences). |

Le `label_dangling` et plusieurs `pin_not_connected` concernent le réseau `BAT_PLUS`/`GND` autour de `BT1`/`J2`/`U1` — les coordonnées calculées correspondent bien aux broches réelles (vérifié), mais KiCad les affiche quand même comme non connectés. Comme pour la version Nano : cliquez l'étiquette et redéplacez-la légèrement pour forcer la reconnexion visuelle, ou ignorez si vous câblez de toute façon ce réseau en dur au routage du PCB.

## Rappel important

`ESPNOW_WIFI_CHANNEL` dans `esp32_buzzer_equipe.ino` doit rester identique à celui du hub (`esp32/esp32_bridge_server.ino`) — sans rapport avec ce schéma, mais à ne pas oublier au premier flash de la carte.

## PCB : placement compact + pistes routées

`buzzer_equipe_esp32.kicad_pcb` : empreintes placées, **et pistes routées** — même pipeline que la carte Nano (Freerouting en CLI headless + réimport KiCad + `kicad-cli pcb drc` réel), voir `../kicad_buzzer_nano/README.md` pour le détail du pipeline.

**Changements suite à ta relecture** :
- **Le texte-guide en sérigraphie a été retiré** — repères de composants standards uniquement (U1L, U1R, SW1...).
- **Carte bien plus compacte** : ~68×101mm au lieu de 130×145mm.
- Pas de keepout sur cette carte : pas de nRF24 ici, l'ESP-NOW utilise l'antenne intégrée à la puce ESP32 elle-même.
- **SW1, SW2, J2, J3 (connecteurs "fils volants") sont maintenant en CMS** (empreintes SMD), par choix explicite pour un assemblage machine (PCBA) chez le fabricant. Même numérotation de broche qu'avant. **Restent traversants** : U1L/U1R (le module ESP32 s'enfiche dessus) et **BT1**.
- **BT1 (batterie) est un vrai connecteur JST-PH** (`JST_PH_B2B-PH-K`, mâle, traversant) — même raisonnement que la carte Nano (voir son README) : la batterie a déjà une prise femelle sertie sur ses fils, et ce connecteur encaisse des cycles de branchement/débranchement qu'un pad CMS ne supporterait pas. Broche 1 = + (rouge), broche 2 = − (noir) — **vérifie la polarité sur ta batterie reçue**.
- **Nouveau : BATLED**, connecteur 2 broches (CMS) pour le module indicateur de niveau de batterie (4 LED) — même composant/câblage que la carte Nano, voir son README pour le détail.

**U1 est un vrai port femelle** : deux barrettes `PinSocket_1x19` (U1L/U1R, 38 broches au total, entraxe 22,86mm — la dimension déjà notée dans `hardware/boitier_buzzer_equipe.scad` pour ce module). Seules 4 des 38 positions physiques sont câblées (GPIO4, GPIO13, VIN, GND) ; les 34 autres sont présentes uniquement pour que le module s'enfiche mécaniquement.

⚠️ **Brochage à vérifier sur le module reçu avant de souder** : contrairement à la carte hub (ESP32 DevKit V1 30 broches, un modèle de référence unique et confirmé par toi), les cartes ESP32 38 broches existent en plusieurs variantes de brochage chez différents fabricants. J'ai utilisé la disposition générique la plus courante ("style NodeMCU-32S", vérifiée sur https://www.espboards.dev/esp32/esp32-38pin-devkit-generic/) : broche réelle 32 = GPIO4, broche 15 = GPIO13, broche 19 = VIN, broche 14 = GND. **Compare ces numéros avec les repères imprimés sur ton module avant de souder** — si ça ne correspond pas, il suffit de déplacer les 4 fils, la carte elle-même n'a pas besoin d'être refaite.

- Aperçu : `apercu_pcb.png`.

**Résultat** : 42 connexions, **0 violation de clearance, 0 élément non connecté**. DRC final : 3 avertissements, tous `lib_footprint_mismatch` (cosmétique, comme sur la carte Nano). Quelques `track_dangling` initiaux (résidus d'autorouter) identifiés et supprimés après vérification de la topologie réelle — **0 track_dangling restant**.

## Prochaine étape

Ouvre le PCB dans KiCad, relis le routage à l'œil, puis passe aux fichiers de fabrication (Gerbers).
