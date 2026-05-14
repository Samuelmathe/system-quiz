#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <string.h>
#include <stdlib.h>

static void dfPlayTrack(DFRobotDFPlayerMini &df, uint8_t track);
static void dfStopTrack(DFRobotDFPlayerMini &df);
static void drainDfSerial(DFRobotDFPlayerMini &df, uint16_t totalMs);
static bool dfErreurPisteAbsent(uint16_t par);
static void jouerBuzzEquipeOuGenerique(DFRobotDFPlayerMini &df, uint8_t team);

// =========================================================
// CONFIGURATION
// =========================================================
RF24 radio(9, 10);

// Pipe "00002" : ordres son depuis la Mega (SonPayload) ou depuis le PC (logiciel score, USB).
// Pipe "00001" : émission vers les nano buzzers (même RadioMsg que mega30Equipe.ino).
const byte adresseBuzzers[6] = "00001";
const byte adresseSon[6]     = "00002";

#define RADIO_CHANNEL 108

/** Même format que mega30Equipe / nano_animateur (CMD 88 = relance, 99 = reset). */
struct RadioMsg {
    uint8_t kind;    // 2 = CMD
    uint8_t value; // 88 / 99
    uint16_t seq;
};
static uint16_t seqOutboundCmd = 0;

static void radioSendCmdToBuzzers(uint8_t value) {
    if (!radioOK) return;
    RadioMsg msg = {2, value, (uint16_t)(++seqOutboundCmd)};
    radio.stopListening();
    radio.openWritingPipe(adresseBuzzers);
    radio.write(&msg, sizeof(msg));
    radio.startListening();
}

// DFPlayer sur broches D4 (TX) et D5 (RX)
SoftwareSerial mySoftwareSerial(5, 4); // RX=D5, TX=D4
DFRobotDFPlayerMini monLecteurMP3;

// =========================================================
// Dossier « mp3 » à la racine de la carte : fichiers nommés 0001.mp3, 0002.mp3, 0101.mp3, etc.
// La lib DFRobot utilise playMp3Folder(N) : N = numero sans zeros de tete (1 -> 0001, 101 -> 0101).
// Ne pas utiliser play(N) : c’est l’index d’ordre sur la carte, pas le nom du fichier.

// Réception depuis la Mega (mega30Equipe.ino) — même contrat que envoyerSon() :
// canal 108, pipe "00002", paquet 4 octets, AutoAck=false côté PRX, PTX en write(..., true)
// NO_ACK ; la Mega envoie 2x 201/202 (~8 ms) — le Nano dedoublonne seulement 201/202, pas le buzz 200.
struct SonPayload {
    uint16_t cmd;
    uint8_t team;  // 1..30 sur buzz ; 0 sinon
    uint8_t reserved;
};

// =========================================================
// VARIABLES
// =========================================================
bool radioOK = false;
bool mp3OK   = false;
unsigned long dernierBattement = 0;

// ----- Port USB (PC) : score + option AUDIO_PC (sons sur PC, DFPlayer muet pour éviter doublon) -----
#define SERIAL_BUF_LEN 80
static char serialBuf[SERIAL_BUF_LEN];
static uint8_t serialLen = 0;
/** Si true : ordres son reçus par radio depuis la Mega sont renvoyés en USB (FWD_SON:...) sans jouer le DF. */
static bool audioViaPc = false;

static void forwardSonToPc(uint16_t cmd, uint8_t team) {
    const unsigned long now = millis();
    if (cmd != 200 && cmd != 201 && cmd != 202) return;

    static uint16_t dedupeCmd = 0xFFFFu;
    static uint8_t dedupeTeam = 0xFFu;
    static unsigned long dedupeAtMs = 0;
    if ((cmd == 201 || cmd == 202) && cmd == dedupeCmd && team == dedupeTeam &&
        (now - dedupeAtMs) < 160UL) {
        return;
    }
    if (cmd == 201 || cmd == 202) {
        dedupeCmd = cmd;
        dedupeTeam = team;
        dedupeAtMs = now;
    }

    if (cmd == 200) {
        Serial.print(F("FWD_SON:200:"));
        Serial.println((unsigned int)team);
    } else if (cmd == 201) {
        Serial.println(F("FWD_SON:201"));
    } else {
        Serial.println(F("FWD_SON:202"));
    }
}

/** Exécute une commande son (200 buzz équipe, 201 victoire, 202 échec) — radio ou USB. */
static void playSonCmd(uint16_t cmd, uint8_t team) {
    if (!mp3OK) return;

    const unsigned long now = millis();

    if (cmd != 200 && cmd != 201 && cmd != 202) return;

    static uint16_t dedupeCmd = 0xFFFFu;
    static uint8_t dedupeTeam = 0xFFu;
    static unsigned long dedupeAtMs = 0;
    if ((cmd == 201 || cmd == 202) && cmd == dedupeCmd && team == dedupeTeam &&
        (now - dedupeAtMs) < 160UL) {
        return;
    }
    if (cmd == 201 || cmd == 202) {
        dedupeCmd = cmd;
        dedupeTeam = team;
        dedupeAtMs = now;
    }

    digitalWrite(LED_BUILTIN, HIGH); delay(50);
    digitalWrite(LED_BUILTIN, LOW);

    if (cmd == 200) {
        jouerBuzzEquipeOuGenerique(monLecteurMP3, team);
    } else if (cmd == 201) {
        dfPlayTrack(monLecteurMP3, 2);
        drainDfSerial(monLecteurMP3, 80);
    } else if (cmd == 202) {
        dfPlayTrack(monLecteurMP3, 3);
        drainDfSerial(monLecteurMP3, 80);
    }
}

static void parseSerialLine(char *line) {
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0') return;

    if (strcmp(line, "WHO") == 0) {
        Serial.println(F("READY_NANO_SON"));
        return;
    }
    if (strcmp(line, "AUDIO_PC") == 0) {
        audioViaPc = true;
        Serial.println(F("OK_AUDIO_PC"));
        return;
    }
    if (strcmp(line, "AUDIO_DF") == 0) {
        audioViaPc = false;
        Serial.println(F("OK_AUDIO_DF"));
        return;
    }

    /* Remplacement Mega (PC non branché sur TTL) : mêmes lignes que le logiciel score → buzzers + son */
    if (strcmp(line, "RESET_ALL") == 0) {
        radioSendCmdToBuzzers(99);
        delay(8);
        Serial.println(F("CMD_SENT:RESET_ALL"));
        if (!audioViaPc) {
            playSonCmd(201, 0);
        }
        return;
    }
    if (strcmp(line, "RELANCE_PARTIEL") == 0) {
        radioSendCmdToBuzzers(88);
        delay(8);
        Serial.println(F("CMD_SENT:RELANCE_PARTIEL"));
        if (!audioViaPc) {
            playSonCmd(202, 0);
        }
        return;
    }

    if (strcmp(line, "STOP") == 0) {
        if (audioViaPc) return;
        if (mp3OK) {
            dfStopTrack(monLecteurMP3);
            drainDfSerial(monLecteurMP3, 60);
        }
        return;
    }
    /* SON:200:N — depuis PC ; en AUDIO_PC le PC joue déjà en pygame */
    if (strncmp(line, "SON:", 4) == 0) {
        if (audioViaPc) return;
        unsigned long c = strtoul(line + 4, NULL, 10);
        if (c == 200) {
            char *colon = strchr(line + 4, ':');
            int team = 0;
            if (colon) team = (int)strtol(colon + 1, NULL, 10);
            if (team < 0) team = 0;
            if (team > 255) team = 255;
            playSonCmd(200, (uint8_t)team);
            return;
        }
        if (c == 201) {
            playSonCmd(201, 0);
            return;
        }
        if (c == 202) {
            playSonCmd(202, 0);
            return;
        }
    }
}

/** SPI nRF24 vs SoftwareSerial : pause radio seulement autour play/stop (pas tout le buzz). */
static void dfPlayTrack(DFRobotDFPlayerMini &df, uint8_t track) {
    if (radioOK) {
        radio.stopListening();
        delay(3);
    }
    /* Fichiers dans dossier « mp3 » : 0001.mp3, 0101.mp3, etc. — utiliser playMp3Folder(N)
     * où N est le numero a 4 chiffres sans zeros de tete (ex. 101 -> 0101.mp3). play() seul = mauvais index. */
    df.playMp3Folder((int)track);
    if (radioOK) {
        delay(5);
        radio.startListening();
    }
}

static void dfStopTrack(DFRobotDFPlayerMini &df) {
    if (radioOK) {
        radio.stopListening();
        delay(3);
    }
    df.stop();
    if (radioOK) {
        delay(5);
        radio.startListening();
    }
}

/** Vide la file serie du DFPlayer (evite que les trames du buzz polluent 201 / 202). */
static void drainDfSerial(DFRobotDFPlayerMini &df, uint16_t totalMs) {
    unsigned long t0 = millis();
    while (millis() - t0 < (unsigned long)totalMs) {
        while (df.available()) {
            (void)df.readType();
            (void)df.read();
        }
        delay(5);
    }
}

/** Vrai « fichier / piste absent » DFPlayer (pas tout octet lu comme erreur sur bus bruite). */
static bool dfErreurPisteAbsent(uint16_t par) {
    uint8_t lo = (uint8_t)(par & 0xFFu);
    uint8_t hi = (uint8_t)((par >> 8) & 0xFFu);
    return lo == 0x06u || lo == 0x09u || hi == 0x06u || hi == 0x09u;
}

/** Son buzz équipe : mp3/01NN si présent ; sinon erreur fichier → stop + 0001.mp3, puis vidage serie. */
static void jouerBuzzEquipeOuGenerique(DFRobotDFPlayerMini &df, uint8_t team) {
    if (team < 1 || team > 30) {
        dfPlayTrack(df, 1);
        drainDfSerial(df, 120);
        return;
    }
    dfPlayTrack(df, (uint8_t)(100 + team));
    delay(280);
    bool useGeneric = false;
    unsigned long t0 = millis();
    while (millis() - t0 < 700UL) {
        if (df.available()) {
            uint8_t t = df.readType();
            uint16_t par = df.read();
            if (t == DFPlayerError && dfErreurPisteAbsent(par)) {
                useGeneric = true;
                break;
            }
        }
        delay(10);
    }
    if (useGeneric) {
        dfStopTrack(df);
        delay(40);
        dfPlayTrack(df, 1);
        delay(40);
    }
    drainDfSerial(df, 200);
}

// =========================================================
// SETUP
// =========================================================
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(9600);

    // 1. DFPlayer alimenté en 3.3V via SoftwareSerial
    mySoftwareSerial.begin(9600);
    if (monLecteurMP3.begin(mySoftwareSerial, false, false)) {
        mp3OK = true;
        monLecteurMP3.setTimeOut(500);
        monLecteurMP3.volume(25);
        monLecteurMP3.EQ(DFPLAYER_EQ_NORMAL);
    }

    // 2. RADIO — aligné sur mega30Equipe / nano_test (pipe 00002, paquet 4 octets)
    if (radio.begin()) {
        radioOK = true;
        radio.setChannel(RADIO_CHANNEL);
        radio.setAddressWidth(5);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setCRCLength(RF24_CRC_16);
        radio.setPayloadSize(sizeof(SonPayload));
        radio.setAutoAck(false);
        radio.openReadingPipe(1, adresseSon);
        radio.startListening();

        // Radio OK -> 3 clignotements
        for (int i = 0; i < 3; i++) {
            digitalWrite(LED_BUILTIN, HIGH); delay(100);
            digitalWrite(LED_BUILTIN, LOW);  delay(100);
        }
    } else {
        // Radio KO -> LED fixe 2 secondes
        digitalWrite(LED_BUILTIN, HIGH);
        delay(2000);
        digitalWrite(LED_BUILTIN, LOW);
    }

    /* Ne pas vider la FIFO RX : la Mega peut deja avoir envoye un paquet son. */
    if (radioOK) {
        delay(5);
        radio.startListening();
    }

    Serial.println(F("READY_NANO_SON"));
}

// =========================================================
// LOOP
// =========================================================
void loop() {
    const unsigned long now = millis();

    for (uint16_t serDrain = 0; serDrain < 128 && Serial.available() > 0; serDrain++) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            serialBuf[serialLen] = '\0';
            while (serialLen > 0 && (serialBuf[serialLen - 1] == ' ' || serialBuf[serialLen - 1] == '\t')) {
                serialBuf[--serialLen] = '\0';
            }
            if (serialLen > 0) parseSerialLine(serialBuf);
            serialLen = 0;
        } else {
            if (serialLen < SERIAL_BUF_LEN - 1) {
                serialBuf[serialLen++] = (uint8_t)c;
            } else {
                serialLen = 0;
            }
        }
    }

    if (radioOK) {
        static unsigned long rearmEcouteMs = 0;
        if (now - rearmEcouteMs > 2500UL) {
            rearmEcouteMs = now;
            radio.startListening();
        }
    }

    // BATTEMENT LED toutes les 2 secondes si radio OK
    if (radioOK && now - dernierBattement > 2000) {
        digitalWrite(LED_BUILTIN, HIGH); delay(50);
        digitalWrite(LED_BUILTIN, LOW);
        dernierBattement = now;
    }

    // RÉCEPTION RADIO (ordres son depuis la Mega, pipe 00002)
    if (radioOK && radio.available()) {
        SonPayload p = {0, 0, 0};
        radio.read(&p, sizeof(p));

        if (p.cmd == 200 || p.cmd == 201 || p.cmd == 202) {
            if (audioViaPc) {
                forwardSonToPc(p.cmd, p.team);
            } else {
                playSonCmd(p.cmd, p.team);
            }
        }
    }
}
