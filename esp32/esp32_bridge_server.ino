// ==========================================================================
// ESP32 BRIDGE & WEBSERVER — SYSTÈME QUIZ DMX (REMPLACE LE RF-NANO)
// ==========================================================================
// Carte recommandée : ESP32 Dev Module / NodeMCU-32S / ESP-WROOM-32
// Carte confirmée commandée par le client pour le hub (régie centrale) :
// ESP32 DevKit V1, puce USB-série FT232, 30 broches, micro-USB. Choisir
// "ESP32 Dev Module" dans Arduino IDE. Pilote USB nécessaire : FTDI VCP
// (pas CP210x/CH340, ce n'est pas la puce de cette carte).
//
// Rôles de l'ESP32 :
//   1. Point d'accès Wi-Fi autonome ("QuizDMX-Pro") + mDNS (quizdmx.local)
//   2. Serveur Web (LittleFS) servant les interfaces animateur.html, config.html,
//      public.html, style.css, app-common.js sans aucune connexion internet
//   3. Serveur WebSocket (/ws) ultra rapide pour synchroniser en temps réel
//      tous les smartphones, tablettes et PC connectés
//   4. Récepteur radio nRF24L01 (Channel 108, 250 kbps) pour tous les buzzers
//      d'équipes (avec fenêtre de 50ms et anti-doublon)
//   5. Émetteur radio vers les buzzers (ordres 99/88 pour LED) et le Nano Son (00002)
//   6. Double liaison UART vers l'Arduino Mega 2560 :
//      - Serial1 (GPIO 25 TX, GPIO 26 RX @ 19200 baud) <-> Mega Serial2 (Lien Jeu/Radio)
//      - Serial2 (GPIO 17 TX, GPIO 16 RX @ 9600 baud)  <-> Mega Serial3 (Lien Config EEPROM)
//   7. Récepteur ESP-NOW (canal Wi-Fi fixe ESPNOW_WIFI_CHANNEL, broadcast) —
//      architecture de SECOURS si les nRF24 posent problème sur site : des
//      boitiers ESP32 (esp32_buzzer_equipe.ino / esp32_animateur.ino) peuvent
//      remplacer un boitier Nano+nRF24 individuellement, sans reflasher ce hub.
//      Meme message RadioMsg{kind,value,seq}, meme dedoublonnage, meme fenetre
//      d'arbitrage de 50ms que le nRF24 — les deux transports coexistent.
//
// ==========================================================================
// CÂBLAGE MATÉRIEL COMPLET
// ==========================================================================
//
// 1) Module nRF24L01+ vers ESP32 :
//    - VCC  -> 3.3V de l'ESP32 (JAMAIS 5V ! Ajouter un condensateur 10µF-100µF entre 3.3V et GND)
//    - GND  -> GND commun
//    - CE   -> GPIO 4
//    - CSN  -> GPIO 5
//    - SCK  -> GPIO 18 (VSPI SCK)
//    - MISO -> GPIO 19 (VSPI MISO)
//    - MOSI -> GPIO 23 (VSPI MOSI)
//
// 2) ESP32 vers Arduino Mega 2560 :
//    - GND ESP32 <-> GND Mega (INDISPENSABLE)
//    - Lien Jeu (19200 baud) :
//        ESP32 TX1 (GPIO 25) -> Mega RX2 (pin 17) [3.3V lu comme HIGH par la Mega : direct OK]
//        ESP32 RX1 (GPIO 26) <- Mega TX2 (pin 16) [5V Mega -> pont diviseur 1kΩ/2kΩ vers ESP32]
//    - Lien Config EEPROM (9600 baud) :
//        ESP32 TX2 (GPIO 17) -> Mega RX3 (pin 14) [direct OK]
//        ESP32 RX2 (GPIO 16) <- Mega TX3 (pin 15) [5V Mega -> pont diviseur 1kΩ/2kΩ vers ESP32]
//
// ==========================================================================

#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <ArduinoJson.h>
#include <esp_now.h>

// --- Configuration Wi-Fi ---
const char* AP_SSID = "QuizDMX-Pro";
const char* AP_PASS = "quizdmx123"; // 8 caractères min (ou "" pour ouvert)

// Mot de passe de la page Régie/Config (config.html) — DOIT être identique
// à CONFIG_PASSWORD dans web/config.js. Changez les DEUX si vous le modifiez :
// le mot de passe du Wi-Fi seul ne protège pas la page contre les autres
// appareils connectés au même réseau pendant l'évènement.
#define CONFIG_PASSWORD "regie2026"

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// Décommentez pour vous connecter en plus au routeur Wi-Fi de la salle :
// const char* STA_SSID = "MonRouteurSalle";
// const char* STA_PASS = "MotDePasseRouteur";

// --- Broches Matérielles ---
#define LED_PIN 2         // LED intégrée ESP32
#define NRF_CE_PIN 4      // nRF24 CE
#define NRF_CSN_PIN 5     // nRF24 CSN

// UART vers Mega Serial2 (Lien Jeu : BUZZ, CMD, SON @ 19200)
#define MEGA_GAME_TX 25   // vers Mega RX2 (pin 17)
#define MEGA_GAME_RX 26   // depuis Mega TX2 (pin 16)
#define MEGA_GAME_BAUD 19200

// UART vers Mega Serial3 (Lien Config EEPROM @ 9600)
#define MEGA_CONF_TX 17   // vers Mega RX3 (pin 14)
#define MEGA_CONF_RX 16   // depuis Mega TX3 (pin 15)
#define MEGA_CONF_BAUD 9600

// --- Instances Serveur & Radio ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
RF24 radio(NRF_CE_PIN, NRF_CSN_PIN);

// --- Protocoles Radio nRF24 (identiques à rf_nano_bridge.ino) ---
const byte adresseBuzzers[6] = "00001";
const byte adresseSon[6]     = "00002";
#define RADIO_CHANNEL 108
#define MAX_EQUIPES   30
#define RADIO_PURGE_MAX 15

bool radioOK = false;

// --- Architecture de secours ESP-NOW (equipes/animateur en ESP32, sans nRF24) ---
// [FIX] Canal Wi-Fi FIXE : ESP-NOW exige que l'emetteur et le recepteur soient
// sur le meme canal. Le point d'acces softAP() doit donc etre demarre sur ce
// meme canal explicitement (voir setup()) -- sinon l'AP peut demarrer sur
// n'importe quel canal choisi par l'ESP32, et les boitiers ESP32 equipes
// (esp32_buzzer_equipe.ino / esp32_animateur.ino, canal code en dur en miroir)
// n'entendraient jamais rien. Changez cette valeur ICI ET dans les deux
// firmwares boitiers si le canal 6 est pollue sur le lieu de l'evenement.
#define ESPNOW_WIFI_CHANNEL 6
static QueueHandle_t espNowQueue = NULL;

struct RadioMsg {
    uint8_t kind;   // 1=BUZZ_EQUIPE, 2=CMD_ANIMATEUR
    uint8_t value;
    uint16_t seq;
};

struct SonPayload {
    uint16_t cmd;
    uint8_t team;
    uint8_t seq;
};

// Anti-doublons radio
static uint16_t lastSeqEquipe[MAX_EQUIPES] = {0};
static bool lastSeqInitEquipe[MAX_EQUIPES] = {false};
static uint16_t seqOutboundCmd = 0;
static uint16_t lastSeqCmd = 0;
static bool lastSeqInitCmd = false;

// Gestion rafale son vers Nano Son
static uint8_t sonSeqCounter = 0;
bool sonEnvoiActif = false;
SonPayload sonQueuePayload;
uint8_t sonPhase = 0;
uint8_t sonBurstIdx = 0;
unsigned long sonProchainEnvoiMs = 0;
uint8_t sonBurstN = 0;
uint8_t sonGapMs = 0;
uint8_t sonPauseMs = 0;

// Fenêtre de simultanéité 50ms
#define FENETRE_MS 50
#define MAX_BUFFER 8
int bufferSignaux[MAX_BUFFER];
int nbBuffer = 0;
bool fenetreActive = false;
unsigned long debutFenetre = 0;

// Buffers UART Mega
char gameBuf[96];
uint8_t gameLen = 0;
char confBuf[128];
uint8_t confLen = 0;

// LED non-bloquante
unsigned long ledPulseUntil = 0;
void ledPulse(unsigned long ms) {
    digitalWrite(LED_PIN, HIGH);
    ledPulseUntil = millis() + ms;
}
void ledUpdate() {
    if (ledPulseUntil != 0 && (long)(millis() - ledPulseUntil) >= 0) {
        digitalWrite(LED_PIN, LOW);
        ledPulseUntil = 0;
    }
}

// ==========================================================================
// RADIO ENVOI COMMANDES & SON
// ==========================================================================
void radioSendCmdToBuzzers(uint8_t value) {
    if (!radioOK) return;
    RadioMsg msg = {2, value, ++seqOutboundCmd};
    radio.stopListening();
    radio.openWritingPipe(adresseBuzzers);
    radio.write(&msg, sizeof(msg));
    radio.openWritingPipe(adresseBuzzers);
    radio.startListening();
}

void envoyerSon(uint16_t cmd, uint8_t team) {
    if (!radioOK) return;
    sonQueuePayload.cmd = cmd;
    sonQueuePayload.team = team;
    sonQueuePayload.seq = (uint8_t)(++sonSeqCounter);

    sonBurstN   = (cmd == 200) ? 4 : 3;
    sonGapMs    = (cmd == 200) ? 12 : 14;
    sonPauseMs  = (cmd == 200) ? 42 : 36;

    sonPhase = 0;
    sonBurstIdx = 0;
    sonEnvoiActif = true;
    sonProchainEnvoiMs = millis();

    radio.stopListening();
    radio.flush_tx();
    radio.openWritingPipe(adresseSon);
    radio.setAutoAck(false);
}

void updateRadioSonAsynchrone() {
    if (!sonEnvoiActif) return;
    unsigned long now = millis();
    if ((long)(now - sonProchainEnvoiMs) < 0) return;

    for (uint8_t retry = 0; retry < 4; retry++) {
        if (radio.write(&sonQueuePayload, sizeof(sonQueuePayload), true)) break;
        delayMicroseconds(300);
    }

    sonBurstIdx++;
    if (sonBurstIdx < sonBurstN) {
        sonProchainEnvoiMs = now + sonGapMs;
    } else {
        if (sonPhase == 0) {
            sonPhase = 1;
            sonBurstIdx = 0;
            sonProchainEnvoiMs = now + sonPauseMs;
        } else {
            sonEnvoiActif = false;
            radio.setAutoAck(true);
            radio.openWritingPipe(adresseBuzzers);
            radio.startListening();
        }
    }
}

bool initRadio() {
    if (!radio.begin()) return false;
    radio.setChannel(RADIO_CHANNEL);
    radio.setAddressWidth(5);
    radio.setPALevel(RF24_PA_LOW);
    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setPayloadSize(sizeof(RadioMsg));
    radio.setAutoAck(true);
    radio.setRetries(10, 15);
    radio.openReadingPipe(1, adresseBuzzers);
    radio.startListening();
    return true;
}

// ==========================================================================
// TRAITEMENT D'UN MESSAGE RADIO (BUZZ equipe ou CMD animateur), quelle que
// soit son origine : nRF24 (radio.available(), boucle loop()) ou ESP-NOW
// (callback onEspNowRecv() -> file d'attente -> videe dans loop()). Les DEUX
// transports partagent le meme dedoublonnage par seq et la meme fenetre
// d'arbitrage de 50ms -- en pratique, une equipe peut etre en nRF24 pendant
// qu'une autre est en ESP-NOW, sans rien changer cote Mega ni cote protocole.
// [FIX] Cette fonction ne doit JAMAIS etre appelee depuis le callback ESP-NOW
// lui-meme (qui tourne sur une autre tache que loop()) : le callback se
// contente d'empiler dans espNowQueue, et c'est loop() (tache unique) qui
// vide la file et appelle cette fonction -- comme ca, bufferSignaux/
// fenetreActive/lastSeq* ne sont jamais touches par deux taches a la fois.
// ==========================================================================
void traiterMessageRadio(const RadioMsg &msg) {
    if (msg.kind == 2) {
        // Commande télécommande animateur sans fil (99=Valider, 88=Refuser)
        if (lastSeqInitCmd && lastSeqCmd == msg.seq) return;
        lastSeqCmd = msg.seq;
        lastSeqInitCmd = true;

        ledPulse(300);
        Serial1.print("CMD:");
        Serial1.println(msg.value);

        if (msg.value == 99) broadcastEvent("CORRECT");
        else if (msg.value == 88) broadcastEvent("WRONG");
        return;
    }

    if (msg.kind == 1) {
        ledPulse(80);
        int signal = (int)msg.value;
        int idx = signal - 1;
        bool duplicate = false;

        if (idx >= 0 && idx < MAX_EQUIPES) {
            if (lastSeqInitEquipe[idx] && lastSeqEquipe[idx] == msg.seq) {
                duplicate = true;
            } else {
                lastSeqEquipe[idx] = msg.seq;
                lastSeqInitEquipe[idx] = true;
            }
        }

        if (!duplicate && signal >= 1 && signal <= MAX_EQUIPES) {
            if (!fenetreActive) {
                fenetreActive = true;
                debutFenetre = millis();
                nbBuffer = 0;
            }
            bool dejaDedans = false;
            for (int i = 0; i < nbBuffer; i++) {
                if (bufferSignaux[i] == signal) {
                    dejaDedans = true;
                    break;
                }
            }
            if (!dejaDedans && nbBuffer < MAX_BUFFER) {
                bufferSignaux[nbBuffer++] = signal;
            }
        }
    }
}

// Callback ESP-NOW : tourne sur la tache Wi-Fi, PAS sur loop(). On se
// contente d'empiler le message recu, sans toucher a l'etat partagé ici.
void onEspNowRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != (int)sizeof(RadioMsg) || !espNowQueue) return;
    RadioMsg msg;
    memcpy(&msg, data, sizeof(msg));
    xQueueSend(espNowQueue, &msg, 0); // non bloquant : on perd le paquet plutot que de bloquer le Wi-Fi
}

// ==========================================================================
// DIFFUSION WEBSOCKET
// ==========================================================================
void broadcastWs(const String &json) {
    ws.textAll(json);
}

void broadcastBuzz(int teamId) {
    StaticJsonDocument<128> doc;
    doc["event"] = "BUZZ";
    doc["team"] = teamId;
    String out;
    serializeJson(doc, out);
    broadcastWs(out);
}

void broadcastEvent(const char* evtName) {
    StaticJsonDocument<128> doc;
    doc["event"] = evtName;
    String out;
    serializeJson(doc, out);
    broadcastWs(out);
}

void broadcastLog(const String &msg, const char* color = "#06B6D4") {
    StaticJsonDocument<256> doc;
    doc["event"] = "LOG";
    doc["msg"] = msg;
    doc["color"] = color;
    String out;
    serializeJson(doc, out);
    broadcastWs(out);
}

// ==========================================================================
// TRAITEMENT CONFIGURATION EEPROM VERS MEGA (Serial2 @ 9600)
// ==========================================================================
void syncConfigToMega(JsonObject root) {
    broadcastLog("Envoi de la configuration vers l'Arduino Mega...", "#FBBF24");

    JsonArray projecteurs = root["projecteurs"];
    JsonArray equipes = root["equipes"];

    int nbEq = equipes.size();
    if (nbEq < 1) nbEq = 1;
    if (nbEq > 30) nbEq = 30;

    // 1. Nombre d'équipes
    Serial2.printf("SET_NB_EQ:%d\n", nbEq);
    delay(30);

    // 2. Patchs et Adresses DMX des projecteurs
    for (size_t i = 0; i < projecteurs.size() && i < 30; i++) {
        JsonObject p = projecteurs[i];
        int id = i;
        int nbCanaux = p["nbCanaux"] | 8;
        int offDim = p["offDim"] | 0;
        int offR = p["offR"] | 1;
        int offG = p["offG"] | 2;
        int offB = p["offB"] | 3;
        int offStrobe = p["offStrobe"] | -1;
        int strobeVal = p["strobeValue"] | 200;
        int strobeRepos = p["strobeRepos"] | 0;
        int mode = p["mode"] | 0;
        int adr = p["adresse"] | (1 + i * 16);

        Serial2.printf("SET_PATCH:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d\n",
            id, nbCanaux, offDim, offR, offG, offB, offStrobe, strobeVal, strobeRepos, mode);
        delay(30);

        Serial2.printf("SET_ADR:%d:%d\n", id, adr);
        delay(30);
    }

    // 3. Couleurs DMX par équipe et projecteur
    for (size_t eqIdx = 0; eqIdx < equipes.size() && eqIdx < 30; eqIdx++) {
        JsonObject eq = equipes[eqIdx];
        int eqNum = eqIdx + 1; // 1-indexé pour le Mega

        // Strobe par équipe
        int strobeMs = eq["strobeDureeMs"] | 0;
        Serial2.printf("SET_STROBE_EQ:%d:%d\n", eqNum, strobeMs);
        delay(20);

        JsonArray clrs = eq["couleurs"];
        for (size_t pIdx = 0; pIdx < projecteurs.size() && pIdx < 30; pIdx++) {
            const char* hex = (pIdx < clrs.size()) ? clrs[pIdx].as<const char*>() : "#FFFFFF";
            if (!hex || hex[0] != '#' || strlen(hex) < 7) hex = "#FFFFFF";

            long rgb = strtol(hex + 1, NULL, 16);
            int r = (rgb >> 16) & 0xFF;
            int g = (rgb >> 8) & 0xFF;
            int b = rgb & 0xFF;

            Serial2.printf("SET_COL:%d:%d:%d:%d:%d\n", eqNum, (int)pIdx, r, g, b);
            delay(25);
        }
    }

    // 4. Sauvegarde permanente EEPROM
    Serial2.println("SAVE_CONFIG");
    delay(50);
    broadcastLog(">> Configuration enregistrée dans l'EEPROM de la Mega !", "#10B981");
}

// ==========================================================================
// GESTION DU WEBSOCKET REÇU
// ==========================================================================
void handleWsMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        StaticJsonDocument<4096> doc;
        DeserializationError error = deserializeJson(doc, (char*)data);
        if (error) return;

        const char* type = doc["type"];
        if (!type) return;

        if (strcmp(type, "VALIDER") == 0) {
            // Relais vers Mega (Serial1 & Serial2)
            Serial1.println("CMD:99");
            Serial2.println("RESET_ALL");
            // Relais radio direct aux buzzers pour réinitialiser les voyants
            radioSendCmdToBuzzers(99);
            broadcastEvent("CORRECT");
            ledPulse(150);
        }
        else if (strcmp(type, "REFUSER") == 0) {
            Serial1.println("CMD:88");
            Serial2.println("RELANCE_PARTIEL");
            radioSendCmdToBuzzers(88);
            broadcastEvent("WRONG");
            ledPulse(150);
        }
        else if (strcmp(type, "RESET_ALL") == 0) {
            Serial1.println("CMD:99");
            Serial2.println("RESET_ALL");
            radioSendCmdToBuzzers(99);
            broadcastEvent("CORRECT");
            ledPulse(150);
        }
        else if (strcmp(type, "SYNC_CONFIG") == 0) {
            // [FIX] Verification cote serveur, pas seulement dans l'UI : la
            // page config.html demande son mot de passe visuellement, mais
            // un appareil connecte au meme Wi-Fi pourrait sinon appeler
            // syncConfigToMega() directement (console navigateur) sans
            // passer par l'ecran de verrouillage. Le mot de passe DOIT donc
            // aussi etre verifie ici, seul rempart reel avant d'ecrire sur
            // la Mega.
            const char* pass = doc["password"];
            if (!pass || strcmp(pass, CONFIG_PASSWORD) != 0) {
                broadcastLog("Synchronisation refusee : mot de passe Regie incorrect.", "#EF4444");
                return;
            }
            JsonObject config = doc["config"];
            syncConfigToMega(config);
        }
    }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("[WS] Client #%u connecte depuis %s\n", client->id(), client->remoteIP().toString().c_str());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("[WS] Client #%u deconnecte\n", client->id());
            break;
        case WS_EVT_DATA:
            handleWsMessage(arg, data, len);
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

// ==========================================================================
// PARSING UART MEGA SERIAL1 (Lien Jeu : 19200 bauds)
// ==========================================================================
static int parse_int(const char *s) { return (int)strtol(s, NULL, 10); }
static bool starts_with(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return false; }
    return true;
}

void parseLigneMegaJeu(const char *line) {
    if (starts_with(line, "CMD:")) {
        int v = parse_int(line + 4);
        radioSendCmdToBuzzers((uint8_t)v);
        if (v == 99) broadcastEvent("CORRECT");
        else if (v == 88) broadcastEvent("WRONG");
    }
    else if (starts_with(line, "SON:")) {
        const char *p = line + 4;
        int cmd = parse_int(p);
        const char *colon = strchr(p, ':');
        int team = colon ? parse_int(colon + 1) : 0;
        envoyerSon((uint16_t)cmd, (uint8_t)team);
    }
    else if (starts_with(line, "BUZZ:")) {
        int team = parse_int(line + 5);
        broadcastBuzz(team);
    }
}

// ==========================================================================
// PARSING UART MEGA SERIAL2 (Lien Config EEPROM : 9600 bauds)
// ==========================================================================
void parseLigneMegaConf(const char *line) {
    // Relais dans les logs console de config.html
    if (starts_with(line, "CONF:")) {
        broadcastLog(String(">> ") + line, "#10B981");
    } else if (starts_with(line, "ERR:")) {
        broadcastLog(String(">> ") + line, "#EF4444");
    } else if (starts_with(line, "READY_MEGA")) {
        broadcastLog("Arduino Mega prête et connectée !", "#3B82F6");
    }
}

// ==========================================================================
// SETUP
// ==========================================================================
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    // USB Série pour débogage
    Serial.begin(115200);
    Serial.println("\n--- DÉMARRAGE ESP32 BRIDGE QUIZ DMX ---");

    // UART1 : vers Mega Serial2 (Lien Jeu / Buzz @ 19200)
    Serial1.begin(MEGA_GAME_BAUD, SERIAL_8N1, MEGA_GAME_RX, MEGA_GAME_TX);

    // UART2 : vers Mega Serial3 (Lien Config EEPROM @ 9600)
    Serial2.begin(MEGA_CONF_BAUD, SERIAL_8N1, MEGA_CONF_RX, MEGA_CONF_TX);

    // Initialisation radio nRF24
    radioOK = initRadio();
    Serial.println(radioOK ? "[RADIO] nRF24 Initialise avec succes" : "[RADIO] ERREUR : echec init nRF24");

    RadioMsg poubelle;
    uint8_t safety = 0;
    while (radio.available() && safety < RADIO_PURGE_MAX) {
        radio.read(&poubelle, sizeof(poubelle));
        safety++;
    }

    // Initialisation LittleFS (fichiers web)
    bool fsOk = LittleFS.begin(true);
    if (fsOk) {
        Serial.println("[FS] LittleFS monte avec succes");
    } else {
        Serial.println("[FS] ERREUR montage LittleFS");
    }

    // Wi-Fi Access Point
    // [FIX] Canal fixe explicite (ESPNOW_WIFI_CHANNEL) : necessaire pour que
    // les boitiers ESP-NOW (equipes/animateur en secours du nRF24) sachent sur
    // quel canal emettre sans avoir a le decouvrir dynamiquement.
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP(AP_SSID, AP_PASS, ESPNOW_WIFI_CHANNEL);
    Serial.printf("[WIFI] Point d'acces actif : SSID='%s', IP=%s, canal=%d\n", AP_SSID, WiFi.softAPIP().toString().c_str(), ESPNOW_WIFI_CHANNEL);

    // ESP-NOW (architecture de secours equipes/animateur, voir esp32_buzzer_equipe.ino
    // / esp32_animateur.ino) : broadcast, aucun appairage MAC necessaire.
    espNowQueue = xQueueCreate(16, sizeof(RadioMsg));
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] ERREUR : init impossible");
    } else {
        esp_now_register_recv_cb(onEspNowRecv);
        Serial.println("[ESPNOW] Pret (broadcast, canal fixe, secours du nRF24)");
    }

    // mDNS (quizdmx.local)
    if (MDNS.begin("quizdmx")) {
        Serial.println("[MDNS] Disponible a l'adresse : http://quizdmx.local");
    }

    // Serveur WebSocket
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // Fichiers statiques depuis LittleFS
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // Route de secours si LittleFS n'est pas encore uploadé
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (LittleFS.exists(request->url())) {
            request->send(LittleFS, request->url());
        } else {
            String html = "<!DOCTYPE html><html lang='fr'><head><meta charset='UTF-8'><title>Quiz DMX Pro</title>"
                          "<style>body{background:#080B11;color:#fff;font-family:sans-serif;padding:40px;text-align:center;}"
                          "h1{color:#8B5CF6;}a{color:#06B6D4;text-decoration:none;font-weight:bold;margin:10px;display:inline-block;padding:12px 20px;border:1px solid #06B6D4;border-radius:8px;}</style></head>"
                          "<body><h1>Quiz DMX Pro — ESP32 Connecté</h1>"
                          "<p>Le serveur est prêt. Si les fichiers web ne sont pas encore téléversés dans LittleFS :</p>"
                          "<div><a href='/animateur.html'>Animateur</a><a href='/config.html'>Configuration</a><a href='/public.html'>Écran Public</a></div>"
                          "<p style='color:#6B7280;margin-top:30px;'>IP ESP32 : " + WiFi.softAPIP().toString() + "</p></body></html>";
            request->send(200, "text/html", html);
        }
    });

    server.begin();
    Serial.println("[HTTP] Serveur Web demarre sur le port 80");

    // Clignotement de confirmation de démarrage
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(60);
        digitalWrite(LED_PIN, LOW);
        delay(60);
    }
}

// ==========================================================================
// LOOP PRINCIPALE
// ==========================================================================
unsigned long prochainStatusMs = 0;
#define STATUS_PERIOD_MS 2000

void loop() {
    ledUpdate();
    updateRadioSonAsynchrone();

    // 1. Heartbeat & auto-réparation radio vers Mega Serial1
    unsigned long now = millis();
    if ((long)(now - prochainStatusMs) >= 0) {
        prochainStatusMs = now + STATUS_PERIOD_MS;
        if (radioOK && !radio.isChipConnected()) {
            radioOK = false;
        }
        if (!radioOK) {
            radioOK = initRadio();
        }
        Serial1.print("STATUS:radioOK=");
        Serial1.println(radioOK ? 1 : 0);
    }

    // 2. Résolution de la fenêtre de buzz (50ms)
    if (fenetreActive && millis() - debutFenetre >= FENETRE_MS) {
        if (nbBuffer > 0) {
            int gagnant = bufferSignaux[0]; // Premier arrivé dans la fenêtre
            Serial1.print("BUZZ:");
            Serial1.println(gagnant);
            broadcastBuzz(gagnant);
        }
        fenetreActive = false;
        nbBuffer = 0;
        RadioMsg p;
        uint8_t safety = 0;
        while (radio.available() && safety < RADIO_PURGE_MAX) {
            radio.read(&p, sizeof(p));
            safety++;
        }
    }

    // 3. Réception Radio nRF24 (Buzzers d'équipes & Télécommande animateur sans fil)
    uint8_t pipeNum;
    while (!sonEnvoiActif && radio.available(&pipeNum)) {
        RadioMsg msg = {0, 0, 0};
        radio.read(&msg, sizeof(msg));
        traiterMessageRadio(msg);
    }

    // 3bis. Réception ESP-NOW (architecture de secours équipes/animateur en
    // ESP32, voir esp32_buzzer_equipe.ino / esp32_animateur.ino). Meme fonction
    // de traitement que le nRF24 ci-dessus : dedoublonnage et fenetre de 50ms
    // partages, qu'importe le transport d'origine du buzz.
    RadioMsg espNowMsg;
    while (espNowQueue && xQueueReceive(espNowQueue, &espNowMsg, 0) == pdTRUE) {
        traiterMessageRadio(espNowMsg);
    }

    // 4. Lecture Mega Serial1 (Lien Jeu / Buzz @ 19200)
    while (Serial1.available() > 0) {
        char c = (char)Serial1.read();
        if (c == '\r') continue;
        if (c == '\n') {
            gameBuf[gameLen] = '\0';
            if (gameLen > 0) parseLigneMegaJeu(gameBuf);
            gameLen = 0;
        } else if (gameLen < sizeof(gameBuf) - 1) {
            gameBuf[gameLen++] = c;
        } else {
            gameLen = 0;
        }
    }

    // 5. Lecture Mega Serial2 (Lien Config EEPROM @ 9600)
    while (Serial2.available() > 0) {
        char c = (char)Serial2.read();
        if (c == '\r') continue;
        if (c == '\n') {
            confBuf[confLen] = '\0';
            if (confLen > 0) parseLigneMegaConf(confBuf);
            confLen = 0;
        } else if (confLen < sizeof(confBuf) - 1) {
            confBuf[confLen++] = c;
        } else {
            confLen = 0;
        }
    }

    // Nettoyage des clients WebSocket inactifs
    ws.cleanupClients();
}
