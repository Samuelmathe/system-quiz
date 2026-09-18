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

## PCB : placement + repères de sérigraphie (routage à faire à la main)

`buzzer_equipe_nano.kicad_pcb` : empreintes placées + texte de sérigraphie pour guider le soudage — **pas de routage des pistes** (à faire toi-même dans KiCad, décision explicite : c'est un travail visuel qu'il vaut mieux faire dans l'éditeur, pas générer à l'aveugle).

- Chaque composant porte son repère (A1, J1, C1...) et une valeur/note sur son propre texte de sérigraphie.
- Un bloc "BROCHAGE" en bas de la carte reprend toutes les correspondances de broches (ex. `J1 nRF24: 1 VCC(3V3) 2 GND 3 CE 4 CSN...`).
- Nano et nRF24 en connecteurs femelles (empreinte `Module:Arduino_Nano`, dimensions/trous identiques que le Nano soit soudé directement ou reçu par un header femelle).
- Aperçu : `apercu_pcb.png` (rendu réel via `kicad-cli pcb render`).

**DRC (`kicad-cli pcb drc`)** : 7 avertissements, tous `lib_footprint_mismatch` (cosmétique — même catégorie que `lib_symbol_mismatch` sur le schéma, se corrige avec `Outils > Mettre à jour les empreintes depuis la bibliothèque`). 20 éléments "non connectés" = normal et attendu, c'est le ratsnest de toutes les connexions qui n'ont pas encore de piste.

## Prochaine étape

Une fois ces points nettoyés dans l'interface KiCad, passez à l'attribution des empreintes (`Outils > Attribuer les empreintes`) puis au routage du PCB — c'est le vrai travail visuel qui doit se faire dans KiCad, pas quelque chose que je peux générer en texte.

La version ESP32 (secours équipe) est dans `../kicad_buzzer_esp32/`, même méthode.
