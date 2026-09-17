# Guide client — Téléverser le système, Wi-Fi, limites

Ce guide couvre l'ensemble des cartes du système : comment reflasher chacune,
combien d'équipes maximum, et comment se connecter en Wi-Fi le jour J.

---

## 1. Vue d'ensemble des cartes

| Rôle | Fichier `.ino` | Carte | Obligatoire ? |
|---|---|---|---|
| Logique de jeu + DMX | `megaf.ino` | Arduino MEGA 2560 | **Oui, toujours** |
| Hub radio + Wi-Fi + serveur web | `esp32/esp32_bridge_server.ino` | ESP32 (Dev Module) | Oui (remplace le RF-Nano) |
| *Alternative au hub ESP32* | `rf_nano_bridge.ino` | RF-Nano (Nano + nRF24 intégré) | Seulement si vous n'utilisez pas l'ESP32 |
| Buzzer d'équipe | `buzzer_nano_equipe.ino` | Nano + module nRF24L01 | Un par équipe |
| *Secours équipe (si nRF24 pose problème)* | `esp32_buzzer_equipe.ino` | ESP32 | Optionnel, en remplacement d'un boîtier équipe |
| Télécommande animateur | `nano_animateur_final.ino` | Nano + module nRF24L01 | Oui |
| *Secours animateur* | `esp32_animateur.ino` | ESP32 | Optionnel, en remplacement du boîtier animateur |
| Son (DFPlayer) | `nano_son_final.ino` | Nano + DFPlayer Mini + module nRF24L01 | Selon le mode choisi (voir [MODES.md](../MODES.md)) |

**Vous n'avez normalement besoin que d'UNE version par rôle** (soit la version Nano+nRF24, soit la version ESP32 de secours) — les deux ne sont utiles ensemble que si vous voulez un boîtier de remplacement prêt en cas de souci radio. Voir [MODES.md](../MODES.md#architecture-de-secours--équipesanimateur-en-esp32--esp-now-sans-nrf24) pour le détail de cette architecture de secours.

---

## 2. Pré-requis communs

1. **Arduino IDE 2.x** installé ([arduino.cc/en/software](https://www.arduino.cc/en/software)).
2. **Câble USB** adapté à chaque carte (micro-USB pour la plupart des Nano/ESP32, parfois USB-C selon le modèle).
3. Si une carte n'apparaît pas dans **Outils > Port** : voir le tableau de dépannage en bas de ce guide.

---

## 3. Téléverser chaque carte

### 3.1 Arduino MEGA 2560 — `megaf.ino`

1. Ouvrez `megaf.ino` dans Arduino IDE.
2. **Outils > Type de carte** : `Arduino Mega or Mega 2560`.
3. **Outils > Processeur** : `ATmega2560 (Mega 2560)`.
4. Aucune bibliothèque externe à installer (`DMXSerial` et `EEPROM` sont soit incluses, soit à installer via le Gestionnaire de bibliothèques si le compilateur les réclame).
5. Sélectionnez le port, cliquez sur **Téléverser**.

### 3.2 ESP32 hub — `esp32/esp32_bridge_server.ino`

Procédure complète (support ESP32, bibliothèques, partition, envoi LittleFS des fichiers web) : voir [APK_HYBRIDE_GUIDE.md](APK_HYBRIDE_GUIDE.md#-téléversement-du-firmware-et-des-fichiers-web-sur-lesp32).

### 3.3 RF-Nano — `rf_nano_bridge.ino` (si vous n'utilisez pas l'ESP32 comme hub)

1. **Outils > Type de carte** : `Arduino Nano`.
2. **Outils > Processeur** : `ATmega328P (Old Bootloader)` si le téléversement échoue avec le processeur standard (fréquent sur les clones).
3. Bibliothèques : `RF24` (par TMRh20).
4. **Débranchez le fil de liaison vers la Mega avant de téléverser** (broches D0/D1 partagées avec l'USB, voir cl.md section pièges connus).

### 3.4 Buzzer d'équipe — `buzzer_nano_equipe.ino`

1. **Outils > Type de carte** : `Arduino Nano` (+ `ATmega328P (Old Bootloader)` si besoin).
2. Bibliothèque : `RF24`.
3. **Avant de téléverser, changez le numéro d'équipe** en haut du fichier :
   ```cpp
   int monEquipe = 3; // <- changez ce chiffre pour chaque boîtier (1 à 30)
   ```
4. Téléversez. Répétez pour chaque équipe avec un numéro différent.

### 3.5 Secours équipe ESP32 — `esp32_buzzer_equipe.ino`

1. **Outils > Type de carte** : `ESP32 Dev Module` (voir §2 d'[APK_HYBRIDE_GUIDE.md](APK_HYBRIDE_GUIDE.md) si le support ESP32 n'est pas encore installé).
2. Changez `int monEquipe = 3;` comme pour la version Nano.
3. Vérifiez que `ESPNOW_WIFI_CHANNEL` (canal 6 par défaut) est **identique** à celui du hub ESP32 — sinon le boîtier n'émettra dans le vide.
4. Téléversez.

### 3.6 Télécommande animateur — `nano_animateur_final.ino` (ou `esp32_animateur.ino`)

Même procédure que le buzzer d'équipe (Nano + `RF24`, ou ESP32 + vérification du canal ESP-NOW pour la version de secours). Pas de numéro d'équipe à changer ici.

### 3.7 Nano son — `nano_son_final.ino`

1. **Outils > Type de carte** : `Arduino Nano`.
2. Bibliothèques : `RF24`, `DFRobotDFPlayerMini`, `SoftwareSerial` (incluse).
3. Préparez la carte microSD du DFPlayer **avant** de l'insérer : dossier `mp3/` à la racine avec les fichiers `0001.mp3`, `0101.mp3`, `0102.mp3`… (voir [MODES.md](../MODES.md#correspondance-des-noms-de-fichiers-équipe-1-)).

---

## 4. Nombre maximum d'équipes

**30 équipes**, en dur dans le code — pas un réglage modifiable depuis les interfaces.

Cette limite vient du dimensionnement des tableaux dans **toutes** les cartes à la fois : `settings.couleurs[30][30][3]`, `adressesDMX[30]`, `profils[30]` et `strobeDureeMs[30]` sur la Mega (`megaf.ino`), `MAX_EQUIPES = 30` sur l'ESP32 (`esp32_bridge_server.ino`), et le plafond de 30 déjà imposé par les deux logiciels Python et l'interface web. La Mega tient tout juste dans ses 4 Ko d'EEPROM avec cette taille (≈3,3 Ko utilisés).

Un plafond encore plus dur existe côté nano son (`nano_son_final.ino`) : `teamTrackKnownOk` est un masque de bits sur 32 bits, donc **32 équipes est le maximum physique absolu** sans changer ce type de donnée.

**Pour dépasser 30**, il faudrait recompiler et reflasher toutes les cartes après avoir augmenté ces tailles de tableau partout à la fois, et revérifier que la Mega tient toujours dans ses 4 Ko d'EEPROM — ce n'est pas un simple réglage, c'est une modification du firmware. À éviter sauf besoin réel.

---

## 5. Connexion Wi-Fi (architecture ESP32)

1. Sur votre téléphone/tablette/PC, ouvrez les réglages Wi-Fi et connectez-vous au réseau :
   - **SSID** : `QuizDMX-Pro`
   - **Mot de passe** : `quizdmx123`
2. Ouvrez un navigateur et allez à :
   - `http://192.168.4.1` (adresse fixe, marche toujours)
   - ou `http://quizdmx.local` (plus simple à retenir, nécessite que l'appareil supporte mDNS — marche sur la plupart des téléphones/PC récents)
3. Vous arrivez sur le portail (`index.html`) : choisissez **Animateur**, **Régie/Configuration** ou **Écran Public** selon l'appareil.
4. **Un seul appareil peut faire la régie/config à la fois**, mais plusieurs animateurs/écrans publics peuvent être connectés simultanément (tous synchronisés en temps réel via WebSocket).
5. **La page Régie/Configuration a son propre mot de passe** (`regie2026` par défaut), distinct du mot de passe Wi-Fi — n'importe qui connecté au réseau peut sinon ouvrir cette page et modifier le DMX en direct. Ce mot de passe est vérifié à deux niveaux : l'écran de verrouillage à l'ouverture de la page (`config.js`), et une seconde vérification côté ESP32 avant tout envoi réel vers la Mega (`esp32_bridge_server.ino`) — la seconde est la vraie protection, la première n'est qu'un confort visuel.
   **Pour le changer** : éditez `CONFIG_PASSWORD` dans **les deux fichiers** (`web/config.js` et `esp32/esp32_bridge_server.ino`), avec la **même valeur**, puis relancez `sync_web_to_esp32.sh` et reflashez le hub ESP32 (voir §3.2).

**Changer le SSID/mot de passe** : modifiables dans `esp32/esp32_bridge_server.ino` (`AP_SSID`, `AP_PASS`), à retéléverser ensuite.

---

## 6. Dépannage rapide

| Symptôme | Solution |
|---|---|
| Carte invisible dans **Outils > Port** | Installer le pilote USB-série (CH340, CP210x ou FTDI VCP selon la carte — cherchez le nom de la puce USB inscrite dessus). |
| Nano ne se laisse pas téléverser | Essayez **Processeur > ATmega328P (Old Bootloader)** (fréquent sur les clones). |
| Page de secours au lieu de l'interface web | Les fichiers `web/` n'ont pas encore été envoyés en LittleFS — voir §3.2. |
| Boîtier ESP32 de secours ne buzz jamais | Vérifiez que `ESPNOW_WIFI_CHANNEL` est identique entre ce boîtier et le hub. |
| Deux enceintes jouent en même temps | Voir [MODES.md](../MODES.md) — un seul chemin son actif à la fois (PC ou DFPlayer), pas les deux. |
| "Synchronisation refusée : mot de passe Régie incorrect" | `CONFIG_PASSWORD` ne correspond pas entre `web/config.js` et `esp32/esp32_bridge_server.ino` — remettez la même valeur des deux côtés et reflashez le hub. |
