# Schéma de départ — Régie/Hub (adaptateur Mega + ESP32 + nRF24)

`regie_hub_mega_esp32.kicad_sch` : même méthode que les deux cartes buzzer (généré, **vérifié avec `kicad-cli sch erc` réel**). 11 composants, 14 nets.

## ⚠️ Correction : broches VCC/GND du nRF24 inversées, module PA+LNA (2×4)

Même correction que sur la carte buzzer Nano (voir `../kicad_buzzer_nano/README.md` pour le détail complet) : **J_NRF avait VCC en broche 1 et GND en broche 2 — inversé par rapport à la vraie numérotation** (broche 1 = GND, broche 2 = VCC), vérifié contre 3 sources indépendantes. Corrigé partout. Ton module confirmé étant un **nRF24L01+PA+LNA** (2×4 broches), l'empreinte J_NRF est passée de `PinSocket_1x08` à `PinHeader_2x04` (connecteur **mâle** — le module a lui-même des ports femelles, donc c'est la carte qui doit porter les broches mâles).

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

## PCB : placement compact + pistes routées

`regie_hub_mega_esp32.kicad_pcb` : empreintes placées, **et pistes routées** — même pipeline que les deux cartes buzzer (Freerouting en CLI headless + réimport KiCad + `kicad-cli pcb drc` réel), voir `../kicad_buzzer_nano/README.md` pour le détail (y compris le mécanisme de keepout, identique ici).

**Changements suite à ta relecture** :
- **Le texte-guide en sérigraphie a été retiré** — repères de composants standards uniquement (J_MEGA, U_ESP32L/R, J_NRF, U_REG, R1-R4, C1-C3), pas de paragraphe d'instructions gravé sur la carte.
- **Carte bien plus compacte** : ~137×85mm au lieu de 185×145mm.
- **C3 (découplage nRF24) encore rapproché de J_NRF** (~12mm, contre 25mm dans la toute première version).
- **Zone sans cuivre sous le nRF24** : même mécanisme keepout que la carte Nano (voir son README pour le détail de la vérification), placé en presqu'île contre le bord droit pour ne bloquer aucun chemin de routage.
- **U_ESP32 est un vrai port femelle** : deux barrettes `PinSocket_1x15` (U_ESP32L/R, 30 broches, entraxe 15,24mm). Seules 11 des 30 positions physiques sont câblées ; les 19 autres sont présentes pour que le module s'enfiche mécaniquement. Brochage : **DOIT ESP32 DevKit V1, 30 broches** — exactement le modèle confirmé commandé, design de référence unique et bien plus standardisé que les clones 38 broches (vérifié sur https://www.espboards.dev/esp32/esp32doit-devkit-v1/).
- **J_NRF (nRF24) est un connecteur MÂLE 2×4** (`PinHeader_2x04`) — le module a des ports femelles dessus, donc la carte porte les broches mâles. 1=GND, 2=VCC, puis CE/CSN/SCK/MOSI/MISO/IRQ. ⚠️ **Vérifie les repères sur ton module avant de souder**, aucun standard universel pour les variantes PA+LNA.
- **R1-R4 et C1-C3 sont maintenant en CMS** (`R_0805`/`C_0805`), par choix explicite pour un assemblage machine (PCBA) chez le fabricant. Même numérotation de broche qu'avant. **Restent traversants** : J_MEGA, U_ESP32L/R et J_NRF (connecteurs de liaison/modules enfichables, pas adaptés au CMS).
- Aperçu : `apercu_pcb.png`.

**Résultat** : 78 connexions (14 nets), **0 violation de clearance, 0 élément non connecté**, 5 vias placées, keepout respecté (0 piste dedans, vérifié par script). DRC final : 12 avertissements, tous `lib_footprint_mismatch` (cosmétique). 4 `track_dangling` initiaux (résidus d'autorouter) identifiés et supprimés après vérification de la topologie réelle — **0 track_dangling restant**.

**C3 (découplage nRF24) rapproché de J_NRF** : le placement initial mettait C3 à 25mm de J_NRF (net `NRF_VCC_DEDIE`, qui relie aussi C2 et U_REG — ce n'est pas un simple lien à 2 broches). Trop loin pour un découplage propre à 2,4GHz. C3 a été repositionné à 12mm de J_NRF (`(152,20)` au lieu de `(165,20)`) et la carte entièrement re-routée : la piste J_NRF↔C3 est maintenant un tronçon direct et court, visible sur `apercu_pcb.png`.

## Prochaine étape

Ouvre le PCB dans KiCad et relis le routage à l'œil, puis passe aux fichiers de fabrication (Gerbers) — en particulier bien respecter les recommandations `cl.md` (VCC nRF24 en 3,3V uniquement jamais 5V, GND commun).
