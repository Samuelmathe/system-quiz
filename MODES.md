# Trois modes d’utilisation — scores & sons (Quiz Board)

Ce document décrit les **trois configurations** possibles entre **Mega** (DMX + logique jeu, `megaf.ino`), **RF-Nano** (pont radio nRF24 ↔ série, `rf_nano_bridge.ino`), **nano son** (DFPlayer + radio), et le **logiciel** `interface/interface_30eq.py` (scores, sons PC, port série).

**Architecture radio (depuis la migration RF-Nano)** : le Mega n’a plus de module nRF24 branché directement (c’était la source d’un gel intermittent diagnostiqué en conditions réelles — connexion physique fragile vers la puce radio). Toute la réception nRF24 (équipes, animateur) et l’émission vers le nano son sont désormais assurées par une carte **RF-Nano** dédiée (Nano + nRF24 intégrés d’usine), reliée au Mega par une simple liaison série filaire (`Serial2`, 19200 bauds). Le Mega envoie/reçoit de simples lignes texte (`BUZZ:n`, `CMD:99`, `SON:200:n`…) au RF-Nano, qui traduit ça en paquets radio et inversement. Cette isolation protège la logique de jeu/DMX : même si le RF-Nano a un problème radio, le Mega continue de tourner.

**Règle matérielle (PC)** : le PC n’est branché en série qu’**à une seule carte à la fois** — soit la **Mega** (TTL / USB), soit le **nano son** (USB), **jamais les deux en même temps**. En mode 3, la Mega reste alimentée pour le DMX et le pilotage du RF-Nano, mais **sans** câble série vers le PC ; tout passe par le nano son (USB).

---

## Vue d’ensemble

| Mode | PC branché ? | Où vont les sons ? | Scores / interface |
|------|----------------|--------------------|---------------------|
| **1 — Sans PC** | Non | **DFPlayer** sur le nano son (carte SD, radio depuis la Mega) | Aucun logiciel |
| **2 — PC sur la Mega** | Oui, **port série TTL** de la Mega (`Serial3` / dongle USB‑UART) | **PC** (`sounds/*.mp3` via pygame) ; le nano son en salle est **optionnel** | Oui : buzz, points, VALIDER/FAUX, `CMD_SENT:` |
| **3 — PC sur le nano son** | Oui, **USB du nano son** | **PC** si mode **AUDIO_PC** (HP du DF débranché ou inutilisé) ; sinon **DFPlayer** si **AUDIO_DF** | Oui : le logiciel détecte le nano (`WHO` → `READY_NANO_SON`) |

---

## Mode 1 — Sans ordinateur

- La **Mega** gère la logique du jeu et le DMX ; elle reçoit les buzz/commandes animateur du **RF-Nano** par liaison série filaire, et lui envoie en retour les ordres **son** (`SON:200:n`, `SON:201:0`, `SON:202:0`) par la même liaison.
- Le **RF-Nano** retransmet ces ordres son en radio vers le **nano son** (pipe `00002`, payload `200` / `201` / `202`) — exactement le même paquet qu’avant, seule l’origine a changé (RF-Nano au lieu de la Mega directement).
- Le **nano son** joue les fichiers sur la **carte microSD** du DFPlayer (dossier `mp3/` sur la carte, ex. `0101.mp3` pour l’équipe 1, repli `0001.mp3`).
- **Pas** de fichier `equipe_1.mp3` sur le PC : tout passe par la convention **DF** (`0101` … `0130`).

---

## Mode 2 — PC branché sur la **Mega** (usage principal)

- Câble **USB‑UART** sur le port prévu pour le PC (souvent **TX3/RX3** de la Mega, voir commentaires dans `megaf.ino`).
- Le logiciel reçoit :
  - **`BUZZ:n`** quand une équipe buzze ;
  - **`CMD_SENT:RESET_ALL`** / **`CMD_SENT:RELANCE_PARTIEL`** quand l’animateur utilise les **boutons physiques** (nano animateur → Mega).
- Les **sons** passent par le dossier **`sounds/`** à côté du script : `buzz.mp3`, `victoire.mp3`, `echec.mp3`, et surtout **`equipe_1.mp3` … `equipe_30.mp3`** pour le buzz par équipe (priorité sur `buzz.mp3`).
- Le **nano son** peut rester en salle pour la même piste radio ; si le PC joue déjà les sons, évite d’avoir **deux** enceintes au même moment (ou baisse le volume d’un des deux chemins).

---

## Mode 3 — PC branché sur le **nano son** (USB)

- Dans le logiciel, choisis le **COM du nano son**. Après connexion, le programme envoie `WHO` : si la réponse contient **`READY_NANO_SON`**, le mode **nano** est actif.
- **Sons sur le PC (`AUDIO_PC`, défaut à la connexion)**  
  - Le nano **ne joue pas** sur le DF pour le buzz : il renvoie **`FWD_SON:200:N`** au PC pour que **pygame** joue (tu peux **débrancher le HP** du module DF pour n’entendre que le PC).
  - **Buzz** : `FWD_SON:200` = même traitement qu’un **`BUZZ:`** en mode 2 (état BUZZÉ, `equipe_N.mp3` si présent).
  - **Valider / refuser** (animateur → Mega → radio **201** / **202** → nano) : le nano envoie **`CMD_SENT:RESET_ALL`** / **`CMD_SENT:RELANCE_PARTIEL`** sur l’USB → **points et état** comme en mode 2, sans second câble vers la Mega.
- **Sons sur la carte SD (`AUDIO_DF`)**  
  - Décoche l’option « sons sur le PC » dans l’interface (ou envoi série `AUDIO_DF`) : le nano rejoue sur le **DFPlayer** comme en mode 1 ; fichiers **`mp3/0101.mp3`** … **`0130.mp3`** (équivalent `equipe_N` côté PC).

### Relais série depuis le PC (nano USB)

- Le logiciel peut envoyer **`RESET_ALL`** / **`RELANCE_PARTIEL`** au nano : le nano **réémet** les commandes radio vers les buzzers (même format que la Mega), utile si la Mega n’est **pas** sur le port PC mais toujours alimentée pour le DMX.

### Fichier de config

- `interface/quiz_board_config.json` (à côté du script) peut contenir entre autres :
  - **`points_au_buzz`** : points ajoutés au **moment du buzz** (0 = désactivé ; la bonne réponse ajoute encore la valeur « PTS » choisie dans l’interface).

---

## Correspondance des noms de fichiers (équipe 1, …)

| Équipe affichée | PC (`sounds/`)     | Carte SD DFPlayer (`mp3/` sur la carte) |
|-----------------|--------------------|----------------------------------------|
| 1               | `equipe_1.mp3`     | `0101.mp3`                             |
| N               | `equipe_N.mp3`     | `01NN.mp3` dans le dossier **`mp3/`** à la racine de la carte (ex. équipe 12 → `0112.mp3`). Le sketch utilise **`playMp3Folder(NNN)`** (commande 0x12) : **101** = fichier **`0101.mp3`**, **1** = **`0001.mp3`** — pas la commande `play()` qui suit l’ordre des fichiers sur la carte. |

La Mega envoie toujours le **numéro d’équipe 1…30** dans le payload radio **200** ; le nano son et le logiciel s’en servent pour choisir la piste ou le fichier.

---

## Fiabilité radio RF-Nano ↔ nano son (nRF24L01)

- **Même canal et adresse** : canal **108**, pipe **`00002`** pour les paquets `SonPayload`.
- **Buzz (200)** : le RF-Nano envoie une rafale de copies espacées de quelques ms (`envoyerSon()`/`updateRadioSonAsynchrone()` dans `rf_nano_bridge.ino`, portées depuis l’ancien `megaf.ino`) avec un **numéro de séquence** (`seq`) ; le nano met les paquets en **file** et **dédoublonne** par `cmd`+`team`+`seq`. DFPlayer : lecture **immédiate** si la piste équipe a déjà réussi une fois, sinon test erreur en fond.
- **Victoire / échec (201 / 202)** : même principe de rafale + dédoublonnage par `seq`.
- **Bonnes pratiques** : alimentation stable (condensateur près du nRF24), antennes correctes, distance raisonnable ; le **250 kbps** aide la portée.

## Fiabilité liaison Mega ↔ RF-Nano (série filaire)

- **`Serial2` sur la Mega (RX2=17, TX2=16)**, **19200 bauds**, GND commun obligatoire.
- Protocole texte simple, une commande par ligne : `BUZZ:n`, `CMD:99`/`CMD:88`, `SON:cmd:team`.
- Le RF-Nano dédoublonne aussi les commandes animateur par `seq` (évite qu’un accusé de réception radio perdu ne fasse rejouer plusieurs fois le son/flash pour un seul appui bouton).
- **Attention en développement** : les broches D0/D1 du RF-Nano sont partagées avec l’USB — débrancher le fil vers la Mega pour reprogrammer/déboguer via USB.

---

## Rappel câblage série (un seul port PC)

| Mode | Câble PC | Lignes reçues par le Quiz Board |
|------|----------|--------------------------------|
| **2 — Mega** | TTL Mega (Serial3) | `BUZZ:n`, `CMD_SENT:…` |
| **3 — Nano son** | USB nano son | `FWD_SON:200:n` (buzz), `CMD_SENT:…` (valider / faux via radio) |

Pas de branchement simultané Mega + nano son sur le même PC.

Pour lancer l’interface et les prérequis Python, voir le [README](README.md).
