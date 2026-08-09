// =========================================================
// RF-NANO — PONT RADIO nRF24 <-> SÉRIE (vers le Mega)
// =========================================================
// Rôle : ce Nano possède désormais la SEULE puce nRF24 du système.
// Il fait 2 choses :
//   1) Écoute les buzz des équipes (pipe "adresseBuzzers"), résout
//      qui a buzzé en premier (fenêtre 50ms + anti-doublon), et
//      envoie "BUZZ:<n>\n" au Mega par liaison série (TX/RX).
//   2) Reçoit du Mega des ordres "CMD:99\n" / "CMD:88\n" (reset/
//      relance) et les rediffuse aux équipes via nRF24, exactement
//      comme le faisait radioSendCmdToBuzzers() sur le Mega avant.
//
// Le son est géré directement par le logiciel PC (Node.js), donc
// aucun relais vers un Nano-Son n'est nécessaire ici.
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

#define LIEN_MEGA_BAUD 19200
#define RADIO_PURGE_MAX 15

RF24 radio(9, 10); // CE, CSN

const byte adresseBuzzers[6]   = "00001";
// Le Nano animateur émet désormais sur adresseBuzzers (voir plus bas) :
// le pipe 2 séparé a été abandonné, test terrain a montré qu'il ne
// s'active pas de façon fiable sur ce clone nRF24.

#define RADIO_CHANNEL 108
#define MAX_EQUIPES   30
#define LED 13

struct RadioMsg {
    uint8_t kind;   // 1=BUZZ_EQUIPE, 2=CMD
    uint8_t value;
    uint16_t seq;
};

// ---- Anti-doublon buzz équipes ----
static uint16_t lastSeqEquipe[MAX_EQUIPES] = {0};
static bool lastSeqInitEquipe[MAX_EQUIPES] = {false};
static uint16_t seqOutboundCmd = 0;

// ---- Anti-doublon commandes animateur (kind==2) ----
static uint16_t lastSeqCmd = 0;
static bool lastSeqInitCmd = false;

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
}

// =========================================================
// SETUP
// =========================================================
void setup() {
    pinMode(LED, OUTPUT);
    Serial.begin(LIEN_MEGA_BAUD); // liaison vers le Mega (partagée avec USB)

    if (radio.begin()) {
        radio.setChannel(RADIO_CHANNEL);
        radio.setAddressWidth(5);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setCRCLength(RF24_CRC_16);
        radio.setPayloadSize(sizeof(RadioMsg));
        radio.setAutoAck(true);
        radio.setRetries(10, 15);
        radio.openReadingPipe(1, adresseBuzzers);
        radio.startListening();
        Serial.println("DEBUG:RADIO_OK,pipes=1+2");
    } else {
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
    uint8_t pipeNum;
    while (radio.available(&pipeNum)) {
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
