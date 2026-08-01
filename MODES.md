# Trois modes d’utilisation — scores & sons (Quiz Board)

Ce document décrit les **trois configurations** possibles entre **Mega** (DMX + logique jeu + radio), **nano son** (DFPlayer + radio), et le **logiciel** `interface/interface_30eq.py` (scores, sons PC, port série).

**Règle matérielle** : le PC n’est branché en série qu’**à une seule carte à la fois** — soit la **Mega** (TTL / USB), soit le **nano son** (USB), **jamais les deux en même temps**. En mode 3, la Mega reste alimentée pour le DMX et la radio, mais **sans** câble série vers le PC ; tout passe par le nano son (USB).

---

## Vue d’ensemble

| Mode | PC branché ? | Où vont les sons ? | Scores / interface |
|------|----------------|--------------------|---------------------|
| **1 — Sans PC** | Non | **DFPlayer** sur le nano son (carte SD, radio depuis la Mega) | Aucun logiciel |
| **2 — PC sur la Mega** | Oui, **port série TTL** de la Mega (`Serial3` / dongle USB‑UART) | **PC** (`sounds/*.mp3` via pygame) ; le nano son en salle est **optionnel** | Oui : buzz, points, VALIDER/FAUX, `CMD_SENT:` |
| **3 — PC sur le nano son** | Oui, **USB du nano son** | **PC** si mode **AUDIO_PC** (HP du DF débranché ou inutilisé) ; sinon **DFPlayer** si **AUDIO_DF** | Oui : le logiciel détecte le nano (`WHO` → `READY_NANO_SON`) |

---

## Mode 1 — Sans ordinateur

- La **Mega** gère buzzers, animateur, DMX et envoie les ordres **son** en radio vers le **nano son** (pipe `00002`, payload `200` / `201` / `202`).
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

## Fiabilité radio Mega ↔ nano son (nRF24L01)

- **Même canal et adresse** : canal **108**, pipe **`00002`** pour les paquets `SonPayload`.
- **Buzz (200)** : la Mega envoie **6 copies** espacées de **~14 ms** (~80 ms au total) avec un **numéro de séquence** (`seq`) ; le nano met les paquets en **file** et **dédoublonne** par `cmd`+`team`+`seq` (~450 ms). DFPlayer : lecture **immédiate** si la piste équipe a déjà réussi une fois (~30 ms), sinon test erreur **~100 ms** max.
- **Victoire / échec (201 / 202)** : **4 copies** ~**18 ms** ; même dédoublonnage par `seq` (~350 ms).
- **Bonnes pratiques** : alimentation stable (condensateur près du nRF24), antennes correctes, distance raisonnable ; le **250 kbps** aide la portée.

---

## Rappel câblage série (un seul port PC)

| Mode | Câble PC | Lignes reçues par le Quiz Board |
|------|----------|--------------------------------|
| **2 — Mega** | TTL Mega (Serial3) | `BUZZ:n`, `CMD_SENT:…` |
| **3 — Nano son** | USB nano son | `FWD_SON:200:n` (buzz), `CMD_SENT:…` (valider / faux via radio) |

Pas de branchement simultané Mega + nano son sur le même PC.

Pour lancer l’interface et les prérequis Python, voir le [README](README.md).
