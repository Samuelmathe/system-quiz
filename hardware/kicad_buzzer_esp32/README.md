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

## PCB : placement + sérigraphie + pistes routées

`buzzer_equipe_esp32.kicad_pcb` : empreintes placées, sérigraphie, **et pistes routées** — même pipeline que la carte Nano (Freerouting en CLI headless + réimport KiCad + `kicad-cli pcb drc` réel), voir `../kicad_buzzer_nano/README.md` pour le détail du pipeline.

- U1 (connecteur 4 broches vers l'ESP32) porte le même brochage que ci-dessus sur sa sérigraphie.
- Bloc "BROCHAGE" en bas de carte + rappel du canal ESPNOW_WIFI_CHANNEL.
- Aperçu : `apercu_pcb.png`.

**Résultat** : 25 connexions, **0 violation de clearance, 0 élément non connecté**. DRC final : 6 avertissements, tous `lib_footprint_mismatch` (cosmétique, comme sur la carte Nano) — aucun `track_dangling` sur cette carte.

## Prochaine étape

Ouvre le PCB dans KiCad, relis le routage à l'œil, puis passe aux fichiers de fabrication (Gerbers).
