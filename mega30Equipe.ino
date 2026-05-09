#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <DMXSerial.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <string.h>
#include <stdlib.h>

// =========================================================
// CONFIGURATION MATÉRIELLE
// =========================================================
RF24 radio(9, 53);

const byte adresseBuzzers[6] = "00001";
const byte adresseSon[6]     = "00002";

#define RADIO_CHANNEL  108

#define LED 13
#define MAGIC_NUMBER 0xAB

// =========================================================
// STRUCTURE EEPROM - 30 équipes max / 30 projecteurs max
// =========================================================
struct QuizConfig {
    byte magic;
    int nbEquipes;
    int nbCanaux;
    int offDim, offR, offG, offB;
    int adressesDMX[30];
    byte couleurs[30][30][3]; // [equipe][projecteur][rgb]
} settings;

// =========================================================
// VARIABLES D'ÉTAT
// =========================================================
bool jeuVerrouille         = false;
bool dejaJoue[31]          = {false};
bool radioOK               = false;
unsigned long dernierBattement  = 0;
int dernierSignal          = -1;
unsigned long dernierSignalTemps = 0;

// =========================================================
// RADIO: Payload fiable (AutoAck + seq)
// =========================================================
struct RadioMsg {
    uint8_t kind;   // 1=BUZZ_EQUIPE, 2=CMD (RESET/RELANCE)
    uint8_t value;  // BUZZ: team 1..30 ; CMD: 88/99
    uint16_t seq;   // increment côté émetteur
};
static uint16_t lastSeqEquipe[30] = {0}; // index 0..29
static uint16_t lastSeqCmd = 0;
static bool lastSeqInitEquipe[30] = {false};
static bool lastSeqInitCmd = false;

// ---- LED non-bloquante (pulse) ----
unsigned long ledPulseUntil = 0;
inline void ledPulse(unsigned long ms) {
    digitalWrite(LED, HIGH);
    ledPulseUntil = millis() + ms;
}
inline void ledUpdate() {
    if (ledPulseUntil != 0 && (long)(millis() - ledPulseUntil) >= 0) {
        digitalWrite(LED, LOW);
        ledPulseUntil = 0;
    }
}

// ---- Serial3 non-bloquant (buffer ligne) ----
#define SERIAL_BUF_LEN 96
char serialBuf[SERIAL_BUF_LEN];
uint8_t serialLen = 0;

// ---- BUZZ SIMULTANÉS - Fenêtre 50ms ----
#define FENETRE_MS   50
#define MAX_BUFFER    8
int  bufferSignaux[MAX_BUFFER];
int  nbBuffer        = 0;
bool fenetreActive   = false;
unsigned long debutFenetre = 0;

// =========================================================
// CONFIG PAR DÉFAUT
// =========================================================
void initialiserConfigParDefaut() {
    settings.magic     = MAGIC_NUMBER;
    settings.nbEquipes = 8;
    settings.nbCanaux  = 8;
    settings.offDim    = 0;
    settings.offR      = 1;
    settings.offG      = 2;
    settings.offB      = 3;

    settings.adressesDMX[0] = 51;
    settings.adressesDMX[1] = 59;
    for (int i = 2; i < 30; i++) settings.adressesDMX[i] = 0;

    byte defCouleurs[8][3] = {
        {255,   0,   0},  // Equipe 1 : Rouge
        {  0,   0, 255},  // Equipe 2 : Bleu
        {  0, 255,   0},  // Equipe 3 : Vert
        {255, 255,   0},  // Equipe 4 : Jaune
        {255,   0, 128},  // Equipe 5 : Rose
        {  0, 255, 255},  // Equipe 6 : Cyan
        {  0, 255,   0},  // Equipe 7 : Vert P1
        {  0,   0, 255},  // Equipe 8 : Bleu P1
    };

    for (int eq = 0; eq < 30; eq++) {
        for (int g = 0; g < 30; g++) {
            if (eq < 8) {
                settings.couleurs[eq][g][0] = defCouleurs[eq][0];
                settings.couleurs[eq][g][1] = defCouleurs[eq][1];
                settings.couleurs[eq][g][2] = defCouleurs[eq][2];
            } else {
                settings.couleurs[eq][g][0] = 255;
                settings.couleurs[eq][g][1] = 255;
                settings.couleurs[eq][g][2] = 255;
            }
        }
    }

    // Equipe 7 P2 = Rose
    settings.couleurs[6][1][0] = 255;
    settings.couleurs[6][1][1] = 0;
    settings.couleurs[6][1][2] = 128;

    // Equipe 8 P2 = Bleu
    settings.couleurs[7][1][0] = 0;
    settings.couleurs[7][1][1] = 0;
    settings.couleurs[7][1][2] = 255;

    EEPROM.put(0, settings);
}

// =========================================================
// ENVOI SIGNAL AU NANO SON
// 200 = buzz valide
// 201 = bonne réponse
// 202 = mauvaise réponse
// =========================================================
void envoyerSon(int signal) {
    radio.stopListening();
    radio.openWritingPipe(adresseSon);
    // Le Nano son est en AutoAck=false -> envoi "fire and forget"
    radio.setAutoAck(false);
    radio.write(&signal, sizeof(signal));
    radio.setAutoAck(true);
    radio.openWritingPipe(adresseBuzzers);
    radio.startListening();
}

// =========================================================
// SETUP
// =========================================================
void setup() {
    wdt_enable(WDTO_8S);
    pinMode(LED, OUTPUT);

    // 1. DMX
    DMXSerial.init(DMXController);
    eteindreLumieres();

    // 2. CHARGEMENT EEPROM
    EEPROM.get(0, settings);
    if (settings.magic != MAGIC_NUMBER ||
        settings.nbEquipes < 1 || settings.nbEquipes > 30 ||
        settings.nbCanaux  < 1 || settings.nbCanaux  > 30) {
        initialiserConfigParDefaut();
    }

    // 3. SERIAL3 pour logiciels PC (broches 14=TX3 / 15=RX3)
    Serial3.begin(9600);

    // 4. RADIO
    if (radio.begin()) {
        radioOK = true;
        radio.setChannel(RADIO_CHANNEL);
        radio.setAddressWidth(5);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setCRCLength(RF24_CRC_16);
        radio.setPayloadSize(sizeof(RadioMsg));
        // Fiabilité: AutoAck + retries (émetteurs doivent aussi être en AutoAck=true)
        radio.setAutoAck(true);
        // Delay=10 (~2500us) donne plus de marge; Count=15 est le max
        radio.setRetries(10, 15);
        radio.openReadingPipe(1, adresseBuzzers);
        radio.startListening();

        for (int i = 0; i < 3; i++) {
            digitalWrite(LED, HIGH); delay(100);
            digitalWrite(LED, LOW);  delay(100);
        }
    } else {
        radioOK = false;
        digitalWrite(LED, HIGH);
        delay(2000);
        digitalWrite(LED, LOW);
    }

    // Vider buffer radio
    delay(100);
    int poubelle;
    while (radio.available()) { radio.read(&poubelle, sizeof(poubelle)); }

    // Projecteurs éteints au démarrage
    eteindreLumieres();
    wdt_reset();
}

// =========================================================
// LOOP PRINCIPALE
// =========================================================
void loop() {

    wdt_reset();
    ledUpdate();

    // Traiter fenêtre expirée
    if (fenetreActive && millis() - debutFenetre >= FENETRE_MS) {
        // Prendre premier signal valide du buffer
        for (int i = 0; i < nbBuffer; i++) {
            int sig = bufferSignaux[i];
            if (sig >= 1 && sig <= settings.nbEquipes && !dejaJoue[sig] && !jeuVerrouille) {
                jeuVerrouille = true;
                dejaJoue[sig] = true;
                allumerCouleurEquipe(sig);
                Serial3.print("BUZZ:"); Serial3.println(sig);
                envoyerSon(200);
                break;
            }
        }
        fenetreActive = false;
        nbBuffer = 0;
        int p; while (radio.available()) { radio.read(&p, sizeof(p)); }
    }

    // BATTEMENT LED toutes les 2 secondes (non-bloquant)
    if (radioOK && millis() - dernierBattement > 2000) {
        ledPulse(50);
        dernierBattement = millis();
    }

    // Reset anti-doublon après 1 seconde
    if (dernierSignal != -1 && millis() - dernierSignalTemps > 1000) {
        dernierSignal = -1;
    }

    // RÉCEPTION SERIAL3 (Logiciels PC) - non bloquant, sans String
    while (Serial3.available() > 0) {
        char c = (char)Serial3.read();
        if (c == '\r') continue;
        if (c == '\n') {
            serialBuf[serialLen] = '\0';
            // trim spaces
            while (serialLen > 0 && (serialBuf[serialLen - 1] == ' ' || serialBuf[serialLen - 1] == '\t')) {
                serialBuf[--serialLen] = '\0';
            }
            uint8_t start = 0;
            while (serialBuf[start] == ' ' || serialBuf[start] == '\t') start++;
            if (serialLen > start) {
                parseCommande(serialBuf + start);
            }
            serialLen = 0;
        } else {
            if (serialLen < SERIAL_BUF_LEN - 1) {
                serialBuf[serialLen++] = c;
            } else {
                // overflow -> reset line
                serialLen = 0;
            }
        }
    }

    // RÉCEPTION RADIO buzzers
    if (radio.available()) {
        RadioMsg msg = {0, 0, 0};
        radio.read(&msg, sizeof(msg));
        int signal = (int)msg.value;

        // Anti-doublon
        // - pour buzz: on filtre par seq par équipe (évite doubles quand on envoie plusieurs fois)
        // - pour CMD: on filtre par seq global
        if (msg.kind == 1) {
            int idx = signal - 1;
            if (idx >= 0 && idx < settings.nbEquipes) {
                if (lastSeqInitEquipe[idx] && lastSeqEquipe[idx] == msg.seq) return;
                lastSeqEquipe[idx] = msg.seq;
                lastSeqInitEquipe[idx] = true;
            }
        } else if (msg.kind == 2) {
            if (lastSeqInitCmd && lastSeqCmd == msg.seq) return;
            lastSeqCmd = msg.seq;
            lastSeqInitCmd = true;
        } else {
            // Message inconnu -> fallback ancien anti-doublon
            if (signal == dernierSignal) return;
        }
        dernierSignal      = signal;
        dernierSignalTemps = millis();

        // Indication réception (non-bloquante)
        ledPulse(60);

        // C'est une équipe → buffer fenêtre simultanée
        if (msg.kind == 1 && signal >= 1 && signal <= settings.nbEquipes) {
            if (!fenetreActive) {
                fenetreActive = true;
                debutFenetre  = millis();
                nbBuffer      = 0;
            }
            bool dejaDedans = false;
            for (int i = 0; i < nbBuffer; i++) {
                if (bufferSignaux[i] == signal) { dejaDedans = true; break; }
            }
            if (!dejaDedans && nbBuffer < MAX_BUFFER) {
                bufferSignaux[nbBuffer++] = signal;
            }
        }

        // 99 = RESET_ALL (Nano animateur bonne réponse)
        else if (msg.kind == 2 && signal == 99) {
            // NOTIFIER LE LOGICIEL EN PREMIER → zéro latence !
            Serial3.println("CMD_SENT:RESET_ALL");
            envoyerSon(201);

            for (int i = 0; i < 31; i++) dejaJoue[i] = false;
            jeuVerrouille = false;
            dernierSignal = -1;

            // Retirer les clignotements DMX bloquants: juste feedback LED
            ledPulse(150);
            eteindreLumieres();

            int poubelle;
            while (radio.available()) { radio.read(&poubelle, sizeof(poubelle)); }
        }

        // 88 = RELANCE_PARTIEL (Nano animateur mauvaise réponse)
        else if (msg.kind == 2 && signal == 88) {
            // NOTIFIER LE LOGICIEL EN PREMIER → zéro latence !
            Serial3.println("CMD_SENT:RELANCE_PARTIEL");
            envoyerSon(202);

            jeuVerrouille = false;
            dernierSignal = -1;

            // Retirer les clignotements DMX bloquants: juste feedback LED
            ledPulse(150);
            eteindreLumieres();

            int poubelle;
            while (radio.available()) { radio.read(&poubelle, sizeof(poubelle)); }
        }
    }
}

// =========================================================
// PARSING COMMANDES (Logiciels PC via Serial3)
// =========================================================
static int parse_int(const char *s, const char **endptr) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (endptr) *endptr = (const char*)end;
    return (int)v;
}

static bool starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return false;
    }
    return true;
}

static uint8_t parse_colon_ints(const char *s, int *out, uint8_t maxCount) {
    uint8_t count = 0;
    const char *p = s;
    while (*p && count < maxCount) {
        // Skip separators
        if (*p == ':') p++;
        // Parse int
        const char *endp = NULL;
        out[count++] = parse_int(p, &endp);
        if (endp == p) break; // no progress
        p = endp;
        // Move to next ':' or end
        while (*p && *p != ':') p++;
    }
    return count;
}

void parseCommande(const char *line) {

    // ---- LOGICIEL SCORES ----

    // Bonne réponse via PC (VALIDER)
    if (strcmp(line, "RESET_ALL") == 0) {
        radio.stopListening();
        radio.openWritingPipe(adresseBuzzers);
        int sig = 99;
        radio.write(&sig, sizeof(sig));
        radio.startListening();
        Serial3.println("CMD_SENT:RESET_ALL");

        envoyerSon(201);
        ledPulse(150);
        eteindreLumieres();

        for (int i = 0; i < 31; i++) dejaJoue[i] = false;
        jeuVerrouille = false;
        dernierSignal = -1;
        fenetreActive = false;
        nbBuffer = 0;
    }

    // Mauvaise réponse via PC (REFUSER)
    else if (strcmp(line, "RELANCE_PARTIEL") == 0) {
        radio.stopListening();
        radio.openWritingPipe(adresseBuzzers);
        int sig = 88;
        radio.write(&sig, sizeof(sig));
        radio.startListening();
        Serial3.println("CMD_SENT:RELANCE_PARTIEL");

        envoyerSon(202);
        ledPulse(150);
        eteindreLumieres();

        jeuVerrouille = false;
        dernierSignal = -1;
        fenetreActive = false;
        nbBuffer = 0;
    }

    // ---- LOGICIEL CONFIG ----

    else if (starts_with(line, "SET_PATCH:")) {
        int vals[5] = {0};
        uint8_t n = parse_colon_ints(line + 9, vals, 5); // after "SET_PATCH"
        if (n >= 5) {
            settings.nbCanaux = vals[0];
            settings.offDim   = vals[1];
            settings.offR     = vals[2];
            settings.offG     = vals[3];
            settings.offB     = vals[4];
            Serial3.println("CONF:PATCH_OK");
        }
    }

    else if (starts_with(line, "SET_NB_EQ:")) {
        int v = parse_int(line + 10, NULL);
        settings.nbEquipes = constrain(v, 1, 30);
        Serial3.println("CONF:NB_EQUIPES_OK");
    }

    else if (starts_with(line, "SET_ADR:")) {
        int vals[2] = {0};
        uint8_t n = parse_colon_ints(line + 7, vals, 2); // after "SET_ADR"
        if (n >= 2) {
            int idx = vals[0];
            int adr = vals[1];
            if (idx >= 0 && idx < 30) settings.adressesDMX[idx] = adr;
            Serial3.println("CONF:ADR_OK");
        }
    }

    else if (starts_with(line, "SET_COL:")) {
        int vals[5] = {0};
        uint8_t n = parse_colon_ints(line + 7, vals, 5); // after "SET_COL"
        if (n >= 5) {
            int eq  = vals[0] - 1;
            int grp = vals[1];
            int r = vals[2], g = vals[3], b = vals[4];
            if (eq >= 0 && eq < 30 && grp >= 0 && grp < 30) {
                settings.couleurs[eq][grp][0] = (byte)constrain(r, 0, 255);
                settings.couleurs[eq][grp][1] = (byte)constrain(g, 0, 255);
                settings.couleurs[eq][grp][2] = (byte)constrain(b, 0, 255);
                Serial3.println("CONF:COL_OK");
            }
        }
    }

    else if (strcmp(line, "SAVE_CONFIG") == 0) {
        settings.magic = MAGIC_NUMBER;
        EEPROM.put(0, settings);
        Serial3.println("CONF:SAVED_TO_EEPROM");
    }

    else if (strcmp(line, "RESET_V1") == 0) {
        initialiserConfigParDefaut();
        Serial3.println("CONF:V1_RESTORED");
    }
}

// =========================================================
// LUMIÈRES DMX
// =========================================================

void setProjecteur(int addr, int r, int g, int b) {
    int maxC = constrain(settings.nbCanaux, 1, 30);
    if (settings.offDim >= 0 && settings.offDim < maxC) DMXSerial.write(addr + settings.offDim, 255);
    if (settings.offR   >= 0 && settings.offR   < maxC) DMXSerial.write(addr + settings.offR,   r);
    if (settings.offG   >= 0 && settings.offG   < maxC) DMXSerial.write(addr + settings.offG,   g);
    if (settings.offB   >= 0 && settings.offB   < maxC) DMXSerial.write(addr + settings.offB,   b);
}

void eteindreLumieres() {
    for (int p = 0; p < 30; p++) {
        int addr = settings.adressesDMX[p];
        if (addr > 0 && addr <= 512) {
            for (int c = 0; c < settings.nbCanaux; c++) {
                DMXSerial.write(addr + c, 0);
            }
        }
    }
}

void clignoterVert() {
    for (int n = 0; n < 3; n++) {
        eteindreLumieres();
        for (int p = 0; p < 30; p++) {
            int addr = settings.adressesDMX[p];
            if (addr > 0 && addr <= 512) setProjecteur(addr, 0, 255, 0);
        }
        delay(150); // Réduit de 300ms à 150ms
        eteindreLumieres();
        delay(150); // Réduit de 300ms à 150ms
    }
}

void clignoterRouge() {
    for (int n = 0; n < 3; n++) {
        eteindreLumieres();
        for (int p = 0; p < 30; p++) {
            int addr = settings.adressesDMX[p];
            if (addr > 0 && addr <= 512) setProjecteur(addr, 255, 0, 0);
        }
        delay(150); // Réduit de 300ms à 150ms
        eteindreLumieres();
        delay(150); // Réduit de 300ms à 150ms
    }
}

void allumerCouleurEquipe(int equipe) {
    eteindreLumieres();
    int idxEq = equipe - 1;
    if (idxEq < 0 || idxEq >= 30) return;

    for (int p = 0; p < 30; p++) {
        int addr = settings.adressesDMX[p];
        if (addr > 0 && addr <= 512) {
            setProjecteur(addr,
                settings.couleurs[idxEq][p][0],
                settings.couleurs[idxEq][p][1],
                settings.couleurs[idxEq][p][2]
            );
        }
    }
}
