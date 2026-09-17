# Guide de Génération des Apps Hybrides & Câblage ESP32

Ce guide vous explique comment transformer les interfaces web (Animateur+Public, Régie/Config) en **applications Android (.apk)**, **desktop (Windows/macOS)**, ce qu'il en est pour iOS, et comment câbler et téléverser le firmware dans l'**ESP32**.

---

## 📱 Option 1 : Installation Instantanée PWA (Recommandée — 0 Compilation)

Grâce au fichier [`manifest.json`](file:///home/samuel/Downloads/dev%20projet/dmxproject/web/manifest.json) et aux métadonnées mobiles configurées :

1. Connectez votre smartphone au réseau Wi-Fi de l'ESP32 : **`QuizDMX-Pro`** (Mot de passe : `quizdmx123`).
2. Ouvrez Google Chrome sur votre téléphone et allez à l'adresse : **`http://192.168.4.1/animateur.html`** (ou `http://quizdmx.local/animateur.html`).
3. Appuyez sur les **3 petits points verticaux** en haut à droite du navigateur.
4. Sélectionnez **« Ajouter à l'écran d'accueil »** ou **« Installer l'application »**.
5. **Résultat :** L'application s'installe avec son icône dédiée, se lance en plein écran sans barre d'adresse et vibre à chaque buzz comme une application native 100% Android !

*Ça marche exactement pareil pour `config.html` (Régie) ou `public.html` — changez juste l'adresse à l'étape 2.*

---

## 🛠️ Option 2 : Compilation d'un véritable fichier `.apk` avec Capacitor

Si vous devez distribuer un fichier installable par clé USB ou WhatsApp, il y a **deux applications séparées** (pas une seule qui bundle tout) :

| App | Dossier | Pages incluses | Pourquoi séparée |
|---|---|---|---|
| **Quiz Animateur** | [`hybrid-app-animateur/`](../hybrid-app-animateur) | `animateur.html` + `public.html` (mini-portail au démarrage) | Usage courant, à distribuer largement |
| **Quiz Régie** | [`hybrid-app-config/`](../hybrid-app-config) | `config.html` uniquement | Pilote le DMX en direct — protégée par son propre mot de passe (voir [GUIDE_CLIENT.md](GUIDE_CLIENT.md)), à ne pas distribuer aussi largement que l'app Animateur |

### Option 2a — Le plus simple : récupérer l'APK déjà compilé (GitHub Actions)

Chaque push sur `main` déclenche automatiquement la compilation des deux APK (workflow **Build Hybrid Apps (Android APK)**) :

1. Allez sur l'onglet **Actions** du dépôt GitHub.
2. Ouvrez le dernier run réussi de **Build Hybrid Apps (Android APK)**.
3. Dans **Artifacts**, téléchargez `hybrid-app-animateur-debug-apk` ou `hybrid-app-config-debug-apk`.
4. Transférez le `.apk` sur le téléphone Android (USB, WhatsApp, e-mail…) et installez-le (autoriser « sources inconnues » si demandé).

C'est un **APK de debug non signé** — parfait pour un usage interne/événementiel, mais pas pour une publication sur le Play Store (ça demanderait une signature de release, hors périmètre ici).

### Option 2b — Compiler soi-même en local

**Prérequis** : [Node.js](https://nodejs.org) (v18+), [Android Studio](https://developer.android.com/studio) avec le SDK Android.

Pour **chaque** app (remplacez `hybrid-app-animateur` par `hybrid-app-config` pour l'autre) :

```bash
cd hybrid-app-animateur
npm install
npm run init:android   # une seule fois : scaffolding du projet Android
npm run sync           # copie les fichiers de web/ dans www/, puis cap copy + cap sync
npm run build:apk      # ./gradlew assembleDebug
```

L'APK généré se trouve dans `hybrid-app-animateur/android/app/build/outputs/apk/debug/app-debug.apk`.

*Pour ouvrir dans Android Studio (déboguer, changer l'icône…) à la place de la ligne de commande :* `npm run open:android`.

> [!IMPORTANT]
> `capacitor.config.json` inclut déjà `"cleartext": true`. C'est **obligatoire** sur Android moderne pour autoriser la connexion au point d'accès Wi-Fi local non-chiffré `http://192.168.4.1` et au WebSocket `ws://192.168.4.1/ws`.

### 🍎 Et pour iOS ?

Un projet Capacitor iOS existe aussi (`npm run init:ios`, `npm run sync`, `npm run open:ios`) et une CI (**Build Hybrid Apps (iOS, verification simulateur)**) vérifie automatiquement qu'il compile — mais **uniquement pour le simulateur, sans signature**. Produire une vraie app installable sur un iPhone demande obligatoirement :

- Un **compte Apple Developer** (payant, ~99$/an),
- Un **Mac avec Xcode** pour l'étape finale d'archivage/signature,
- Des certificats et profils de provisionnement configurés dans le compte Apple.

Rien de tout ça n'est automatisable sans que vous (ou le client) fournissiez ce compte — c'est une contrainte d'Apple, pas une limite de ce projet. Si vous avez un compte Apple Developer, ouvrez `ios/App.xcworkspace` dans Xcode après `npm run sync` et suivez le flux Apple habituel (Signing & Capabilities → Archive → distribuer).

---

## 🖥️ Option 3 : Application desktop (Windows / macOS)

Utile pour un client qui préfère une icône dédiée sur le Bureau plutôt que d'ouvrir un navigateur et taper `192.168.4.1` — même principe que les apps mobiles, via [`@capawesome/capacitor-electron`](https://capawesome.io/docs/sdks/capacitor/electron/) (la plateforme Electron **maintenue** pour Capacitor 6 ; l'ancienne `@capacitor-community/electron` est abandonnée et connue pour des soucis de compatibilité avec Capacitor 6 — ne pas l'utiliser).

### Option 3a — Récupérer l'installeur déjà compilé (GitHub Actions)

Chaque push sur `main` compile automatiquement les deux apps pour **Windows et macOS** (workflow **Build Hybrid Apps (Desktop Windows + macOS)**) :

1. Onglet **Actions** du dépôt GitHub → dernier run réussi de ce workflow.
2. Dans **Artifacts**, téléchargez `hybrid-app-animateur-desktop-windows-latest`, `hybrid-app-animateur-desktop-macos-latest`, ou l'équivalent `hybrid-app-config-desktop-*`.
3. Windows : lancez le `.exe`. macOS : ouvrez le `.dmg` et glissez l'app dans Applications (macOS peut demander d'autoriser l'app dans Réglages Système → Confidentialité et sécurité, l'app n'étant pas signée par un compte Apple Developer — voir note ci-dessous).

### Option 3b — Compiler soi-même en local

**Prérequis** : [Node.js](https://nodejs.org) (v18+).

```bash
cd hybrid-app-animateur
npm install
npm run init:electron    # une seule fois : ajoute la plateforme Electron
npm run sync:electron    # copie web/ dans www/, puis cap sync vers electron/
npm run build:electron   # compile l'installeur (electron-builder)
```

L'installeur se trouve dans `hybrid-app-animateur/electron/dist/` (`.exe` sous Windows, `.dmg`/`.zip` sous macOS — vous ne pouvez compiler que pour l'OS sur lequel vous lancez la commande, comme pour n'importe quel outil Electron).

> [!NOTE]
> Comme les APK, ce sont des builds **non signés** (aucun certificat de signature de code Windows/Apple configuré). Windows peut afficher un avertissement SmartScreen, macOS peut bloquer l'app au premier lancement (clic droit → Ouvrir, ou l'autoriser dans Réglages Système). Suffisant pour un usage interne/événementiel ; une distribution plus large demanderait un certificat de signature (payant, hors périmètre ici).

---

## 🔌 Câblage Matériel Complet : ESP32 ↔ nRF24L01+ ↔ Arduino Mega 2560

### 1. nRF24L01+ vers ESP32 (Bus SPI VSPI)
| Broche nRF24L01 | Broche ESP32 | Remarques critiques |
| :--- | :--- | :--- |
| **VCC** | **3.3V** | ⚠️ **JAMAIS LE 5V !** Risque de destruction immédiate de la puce RF. |
| **GND** | **GND** | Masse commune. |
| **CE** | **GPIO 4** | Activation émetteur/récepteur. |
| **CSN** | **GPIO 5** | Chip Select SPI. |
| **SCK** | **GPIO 18** | Horloge matérielle SPI. |
| **MISO** | **GPIO 19** | Données entrantes SPI. |
| **MOSI** | **GPIO 23** | Données sortantes SPI. |

> [!TIP]
> **Condensateur de découplage nRF24** : Soudez un condensateur électrolytique de **10 µF à 100 µF** directement entre les broches VCC et GND du module nRF24. Cela élimine 99% des bruits de transmission et évite les blocages radio.

---

### 2. ESP32 vers Arduino Mega 2560
| Fonction | Sortie ESP32 | Entrée Mega 2560 | Adaptation de tension |
| :--- | :--- | :--- | :--- |
| **Masse** | **GND** | **GND** | **Liaison directe obligatoire** |
| **Jeu (Buzz / Commandes)** | **GPIO 25 (TX1)** | **Pin 17 (RX2)** | Direct (Le Mega 5V accepte le 3.3V HIGH) |
| **Jeu (Retours Mega)** | **GPIO 26 (RX1)** | **Pin 16 (TX2)** | **Pont diviseur 1kΩ / 2kΩ** (5V ➔ 3.3V) |
| **Config EEPROM** | **GPIO 17 (TX2)** | **Pin 14 (RX3)** | Direct |
| **Config (Retours Mega)** | **GPIO 16 (RX2)** | **Pin 15 (TX3)** | **Pont diviseur 1kΩ / 2kΩ** (5V ➔ 3.3V) |

*Schéma du pont diviseur recommandé pour chaque fil 5V Mega TX -> ESP32 RX :*
```text
Mega TX (5V) ───[ 1 kΩ ]───┬───> ESP32 RX (3.3V)
                           │
                       [ 2 kΩ ]
                           │
                          GND
```

---

## 🚀 Téléversement du Firmware et des Fichiers Web sur l'ESP32

### 0. Préparer Arduino IDE (une seule fois, si ce n'est pas déjà fait)

1. Installez **Arduino IDE 2.x** (la version actuelle, [arduino.cc/en/software](https://www.arduino.cc/en/software)).
2. Ajoutez le support des cartes ESP32 :
   - Menu **Fichier > Préférences** (ou **Arduino IDE > Paramètres** sur Mac).
   - Dans **« URL de gestionnaire de cartes supplémentaires »**, collez :
     ```
     https://espressif.github.io/arduino-esp32/package_esp32_index.json
     ```
   - **Outils > Type de carte > Gestionnaire de cartes**, cherchez `esp32` (par Espressif Systems) et installez-le.
   - Redémarrez Arduino IDE.
3. Installez les bibliothèques requises via **Outils > Gérer les bibliothèques** (cherchez chaque nom, installez la version proposée en premier) :
   - `ESPAsyncWebServer`
   - `AsyncTCP`
   - `RF24` (par TMRh20)
   - `ArduinoJson` (v6 ou plus récent)
   - *(`WiFi`, `ESPmDNS`, `LittleFS`, `SPI` sont déjà inclus avec le support ESP32, rien à installer)*
4. Installez l'outil d'envoi LittleFS (nécessaire pour téléverser les fichiers `web/` séparément du code) :
   - Téléchargez le fichier `.vsix` le plus récent depuis [github.com/earlephilhower/arduino-littlefs-upload/releases](https://github.com/earlephilhower/arduino-littlefs-upload/releases).
   - Placez-le dans le dossier `~/.arduinoIDE/plugins/` (le créer s'il n'existe pas ; sous Windows : `C:\Users\<votre-nom>\.arduinoIDE\plugins\`).
   - Redémarrez Arduino IDE.

### 1. Téléverser le code Arduino :
1. Ouvrez [`esp32/esp32_bridge_server.ino`](file:///home/samuel/Downloads/dev%20projet/dmxproject/esp32/esp32_bridge_server.ino) dans l'Arduino IDE.
2. Choisissez le type de carte : **Outils > Type de carte > ESP32 Arduino > ESP32 Dev Module** (ou **NodeMCU-32S**).
3. **Outils > Partition Scheme** : choisissez **« Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS) »** (ou un schéma offrant au moins 1.5 Mo pour les fichiers web — le dossier `esp32/data/` fait environ 1,3 Mo).
4. Branchez l'ESP32 en USB, sélectionnez le bon port dans **Outils > Port**, puis cliquez sur **Téléverser**.

### 2. Téléverser l'interface Web dans LittleFS :
1. Lancez le script de synchronisation pour copier les dernières modifications de `web/` vers `esp32/data/` :
   ```bash
   ./sync_web_to_esp32.sh
   ```
2. Dans Arduino IDE 2.x : `Ctrl+Maj+P` (ou `⌘+Maj+P` sur Mac), tapez et sélectionnez **« Upload LittleFS to Pico/ESP8266/ESP32 »**.
   *(Fermez le Moniteur Série avant de lancer l'envoi, sinon le port est occupé et l'envoi échoue.)*
3. Démarrez l'ESP32 : il crée automatiquement le Wi-Fi `QuizDMX-Pro` et sert immédiatement les interfaces !

### 🩹 Dépannage courant

| Symptôme | Cause probable | Solution |
| :--- | :--- | :--- |
| Aucun port ne s'affiche dans **Outils > Port** | Pilote USB-série manquant | Installez le pilote correspondant à la puce de votre carte ESP32 (**FTDI VCP** pour la carte ESP32 DevKit V1/FT232 confirmée pour le hub régie ; sinon souvent **CP210x** ou **CH340** selon le fabricant — cherchez le nom exact inscrit sur la puce USB de votre carte + « driver » sur le site du fabricant). |
| Erreur *"Sketch too big"* / dépasse l'espace disponible | Partition Scheme trop petite pour le code | Reprenez l'étape 1.3 : choisissez un schéma avec plus d'espace APP. |
| L'envoi LittleFS échoue ou le port se ferme tout seul | Moniteur Série encore ouvert, ou mauvais port sélectionné | Fermez le Moniteur Série, revérifiez **Outils > Port**. |
| Page de secours au lieu de l'interface | Fichiers `web/` pas encore envoyés en LittleFS | Refaites l'étape 2 ci-dessus (le firmware seul ne suffit pas, il faut aussi l'envoi LittleFS séparé). |
