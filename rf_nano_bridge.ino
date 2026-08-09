// =========================================================
// RF-NANO — PONT RADIO nRF24 <-> SÉRIE (vers le Mega)
// =========================================================
// Rôle : ce Nano possède désormais la SEULE puce nRF24 du système.
// Il fait 3 choses :
//   1) Écoute les buzz des équipes (pipe "adresseBuzzers"), résout
//      qui a buzzé en premier (fenêtre 50ms + anti-doublon), et
//      envoie "BUZZ:<n>\n" au Mega par liaison série (TX/RX).
//   2) Reçoit du Mega des ordres "CMD:99\n" / "CMD:88\n" (reset/
//      relance) et les rediffuse aux équipes via nRF24, exactement
//      comme le faisait radioSendCmdToBuzzers() sur le Mega avant.
//   3) Reçoit du Mega des ordres son "SON:<cmd>:<team>\n" et les
//      retransmet en radio au nano son (pipe "adresseSon"), exactement
//      comme le faisait envoyerSon() sur le Mega avant — nécessaire
//      pour le mode sans PC (le logiciel PC, quand présent, joue le
//      son de son côté à réception de BUZZ:/CMD_SENT:, indépendamment
//      de ce relais radio).
//
// Câblage vers le Mega (Serial2) :
//   Nano TX (D1)  -> Mega RX2 (pin 17)
//   Nano RX (D0)  <- Mega TX2 (pin 16)
//   GND commun obligatoire
// Baudrate : 19200 des deux côtés.
//
// ATTENTION : D0/D1 sont partagées avec l'USB. Pour déboguer via
// USB, débranche temporairement le fil vers le Mega (sinon conflit).
//
// Pins nRF24 (standard RF-Nano / shield Nano) : CE=9, CSN=10,
// MOSI=11, MISO=12, SCK=13 (SPI matériel, ne pas changer).
// Vérifie ce pinout dans la doc github.com/nulllaborg/rf-nano si
// ta carte diffère.
// =========================================================

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <string.h>
#include <stdlib.h>

#define LIEN_MEGA_BAUD 19200
#define RADIO_PURGE_MAX 15

RF24 radio(9, 10); // CE, CSN

const byte adresseBuzzers[6]   = "00001";
// Le Nano animateur émet désormais sur adresseBuzzers (voir plus bas) :
// le pipe 2 séparé a été abandonné, test terrain a montré qu'il ne
// s'active pas de façon fiable sur ce clone nRF24.
const byte adresseSon[6]       = "00002"; // vers le nano son, cf. nano_son_final.ino

#define RADIO_CHANNEL 108
#define MAX_EQUIPES   30
#define LED 13

bool radioOK = false;

struct RadioMsg {
    uint8_t kind;   // 1=BUZZ_EQUIPE, 2=CMD
    uint8_t value;
    uint16_t seq;
};

// Doit rester identique à la struct SonPayload de nano_son_final.ino
// (même taille en octets, même ordre de champs).
struct SonPayload {
    uint16_t cmd;
    uint8_t team;
    uint8_t seq;
};

// ---- Anti-doublon buzz équipes ----
static uint16_t lastSeqEquipe[MAX_EQUIPES] = {0};
static bool lastSeqInitEquipe[MAX_EQUIPES] = {false};
static uint16_t seqOutboundCmd = 0;

// ---- Anti-doublon commandes animateur (kind==2) ----
static uint16_t lastSeqCmd = 0;
static bool lastSeqInitCmd = false;

// ---- Envoi son vers le nano son, en rafale non bloquante (portée depuis
// l'ancien megaf.ino : envoyerSon()/updateRadioSonAsynchrone()) ----
static uint8_t sonSeqCounter = 0;
bool sonEnvoiActif = false;
SonPayload sonQueuePayload;
uint8_t sonPhase = 0;
uint8_t sonBurstIdx = 0;
unsigned long sonProchainEnvoiMs = 0;
uint8_t sonBurstN = 0;
uint8_t sonGapMs = 0;
uint8_t sonPauseMs = 0;

// ---- Fenêtre de simultanéité 50ms (identique à l'ancienne logique Mega) ----
#define FENETRE_MS 50
#define MAX_BUFFER 8
int  bufferSignaux[MAX_BUFFER];
int  nbBuffer = 0;
bool fenetreActive = false;
unsigned long debutFenetre = 0;

// ---- Buffer ligne série (venant du Mega) ----
#define SERIAL_BUF_LEN 64
char serialBuf[SERIAL_BUF_LEN];
uint8_t serialLen = 0;

// ---- LED de diagnostic non-bloquante ----
unsigned long ledPulseUntil = 0;
void ledPulse(unsigned long ms) {
    digitalWrite(LED, HIGH);
    ledPulseUntil = millis() + ms;
}
void ledUpdate() {
    if (ledPulseUntil != 0 && (long)(millis() - ledPulseUntil) >= 0) {
        digitalWrite(LED, LOW);
        ledPulseUntil = 0;
    }
}

// =========================================================
// ENVOI D'UNE COMMANDE (99/88) AUX ÉQUIPES
// =========================================================
void radioSendCmdToBuzzers(uint8_t value) {
    RadioMsg msg = {2, value, ++seqOutboundCmd};
    radio.stopListening();
    radio.openWritingPipe(adresseBuzzers);
    radio.write(&msg, sizeof(msg));
    radio.openWritingPipe(adresseBuzzers);
    radio.startListening();
}

// =========================================================
// ENVOI SON VERS LE NANO SON (pipe "adresseSon"), sans bloquer la
// réception des buzz équipes — même logique de rafale que l'ancien
// megaf.ino (4 copies espacées pour le buzz, 3 pour valider/refuser).
// =========================================================
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
            // Fin de la rafale -> retour en écoute des buzzers
            sonEnvoiActif = false;
            radio.setAutoAck(true);
            radio.openWritingPipe(adresseBuzzers);
            radio.startListening();
        }
    }
}

// =========================================================
// PARSING DES LIGNES REÇUES DU MEGA
// =========================================================
static int parse_int(const char *s) { return (int)strtol(s, NULL, 10); }

static bool starts_with(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return false; }
    return true;
}

void parseLigneMega(const char *line) {
    if (starts_with(line, "CMD:")) {
        int v = parse_int(line + 4);
        radioSendCmdToBuzzers((uint8_t)v);
    }
    else if (starts_with(line, "SON:")) {
        // Format attendu : SON:<cmd>:<team>  (ex. SON:200:8, SON:201:0, SON:202:0)
        const char *p = line + 4;
        int cmd = parse_int(p);
        const char *colon = strchr(p, ':');
        int team = colon ? parse_int(colon + 1) : 0;
        if (team < 0) team = 0;
        if (team > 255) team = 255;
        envoyerSon((uint16_t)cmd, (uint8_t)team);
    }
}

// =========================================================
// SETUP
// =========================================================
void setup() {
    pinMode(LED, OUTPUT);
    Serial.begin(LIEN_MEGA_BAUD); // liaison vers le Mega (partagée avec USB)

    if (radio.begin()) {
        radioOK = true;
        radio.setChannel(RADIO_CHANNEL);
        radio.setAddressWidth(5);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setCRCLength(RF24_CRC_16);
        // RadioMsg et SonPayload font toutes deux 4 octets — même payloadSize
        // valable pour l'écoute équipes/animateur et l'envoi vers le nano son.
        radio.setPayloadSize(sizeof(RadioMsg));
        radio.setAutoAck(true);
        radio.setRetries(10, 15);
        radio.openReadingPipe(1, adresseBuzzers);
        radio.startListening();
        Serial.println("DEBUG:RADIO_OK,pipes=1+2");
    } else {
        radioOK = false;
        Serial.println("DEBUG:RADIO_INIT_FAILED");
    }

    RadioMsg poubelle;
    uint8_t safety = 0;
    while (radio.available() && safety < RADIO_PURGE_MAX) {
        radio.read(&poubelle, sizeof(poubelle));
        safety++;
    }
}

// =========================================================
// LOOP
// =========================================================
void loop() {
    ledUpdate();
    updateRadioSonAsynchrone(); // rafale son en arrière-plan, non bloquant

    // Fenêtre expirée : on décide qui a buzzé en premier
    if (fenetreActive && millis() - debutFenetre >= FENETRE_MS) {
        if (nbBuffer > 0) {
            int gagnant = bufferSignaux[0]; // premier arrivé dans la fenêtre
            Serial.print("BUZZ:");
            Serial.println(gagnant);
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

    // Lecture des ordres venant du Mega
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            serialBuf[serialLen] = '\0';
            if (serialLen > 0) parseLigneMega(serialBuf);
            serialLen = 0;
        } else if (serialLen < SERIAL_BUF_LEN - 1) {
            serialBuf[serialLen++] = c;
        } else {
            serialLen = 0;
        }
    }

    // Réception radio : équipes (pipe 1, fenêtre 50ms) ou animateur (pipe 2, direct/fiable)
    // Suspendue pendant une rafale son (radio en mode émission vers adresseSon).
    uint8_t pipeNum;
    while (!sonEnvoiActif && radio.available(&pipeNum)) {
        RadioMsg msg = {0, 0, 0};
        radio.read(&msg, sizeof(msg));

        Serial.print("DEBUG:RX pipe="); Serial.print(pipeNum);
        Serial.print(" kind="); Serial.print(msg.kind);
        Serial.print(" value="); Serial.println(msg.value);

        // On route désormais sur le champ "kind" du message, pas sur le
        // numéro de pipe : sur certains nRF24 (clones notamment), la
        // distinction entre pipes 1-5 (qui ne diffèrent que par leur
        // dernier octet) n'est pas fiable et peut mal classer un message.
        if (msg.kind == 2) {
            // Message venant du Nano animateur (identifié par son kind)
            // Dédoublonnage par seq : si l'accusé de réception radio se perd,
            // la puce nRF24 de la télécommande retransmet automatiquement le
            // même paquet (même seq) — sans ce filtre, le Mega recevrait
            // plusieurs CMD:99/88 pour un seul appui bouton.
            if (lastSeqInitCmd && lastSeqCmd == msg.seq) {
                continue; // doublon, ignoré
            }
            lastSeqCmd = msg.seq;
            lastSeqInitCmd = true;

            ledPulse(300); // pulse longue = commande animateur reçue
            Serial.print("CMD:");
            Serial.println(msg.value);
            continue;
        }

        if (msg.kind == 1) {
            ledPulse(80); // pulse courte = buzz équipe brut reçu (avant même filtrage)
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
                    if (bufferSignaux[i] == signal) { dejaDedans = true; break; }
                }
                if (!dejaDedans && nbBuffer < MAX_BUFFER) {
                    bufferSignaux[nbBuffer++] = signal;
                }
            }
        }
    }
}
