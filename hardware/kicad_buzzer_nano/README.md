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
| `label_dangling` | 3 | **À vérifier** — `BAT_PLUS`, `NRF_SCK`, `VCC_5V_SW` : l'étiquette semble pile sur la bonne broche (vérifié par calcul), mais KiCad la signale quand même comme non connectée. Je n'ai pas trouvé la cause exacte. Cliquez sur l'étiquette et déplacez-la d'un cran (ou re-tapez-la au même endroit) pour forcer la reconnexion — 10 secondes chacune. |
| `no_connect_dangling` | 1 | Le marqueur "pas de connexion" sur la broche IRQ du module nRF24 (non utilisée par le firmware) est légèrement décalé — à recaler d'un clic sur la broche. |
| `power_pin_not_driven` / `pin_not_driven` | 4 + 3 | À vérifier au cas par cas dans l'ERC, probablement lié aux mêmes broches Nano non utilisées. |

## Prochaine étape

Une fois ces points nettoyés dans l'interface KiCad, passez à l'attribution des empreintes (`Outils > Attribuer les empreintes`) puis au routage du PCB — c'est le vrai travail visuel qui doit se faire dans KiCad, pas quelque chose que je peux générer en texte.

La version ESP32 (secours équipe) n'est pas encore générée — même principe à reprendre si besoin, avec le BOM déjà prêt dans `../../docs/BOM_BUZZER_EQUIPE.md`.
