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
static void pollRadioRx(void);
static bool dfProbeTrackMissing(DFRobotDFPlayerMini &df, uint8_t track, uint16_t maxMs);
static void sonProbeSdAtBoot(DFRobotDFPlayerMini &df);

/** Delais DF / radio (latence vs fiabilite). */
#define DF_ERR_POLL_MS      100
#define DF_PROBE_BOOT_MS     55
#define DF_DRAIN_SHORT_MS    28
#define DF_DRAIN_CMD_MS      40
/** Volume DFPlayer 0..30 (modifier pour le client si besoin). */
#define DF_VOLUME           25

/** Paquet radio Mega → nano (pipe 00002), 4 octets, NO_ACK. */
struct SonPayload {
    uint16_t cmd;
    uint8_t team;
    uint8_t seq;
};

#define SON_RX_Q_LEN 12
/** Re-arme l'ecoute PRX (filet si SPI/DF a laisse le chip hors RX). */
#define SON_REARM_LISTEN_MS 400
static SonPayload sonRxQ[SON_RX_Q_LEN];
static uint8_t sonRxQHead = 0;
static uint8_t sonRxQTail = 0;

static void playSonCmd(uint16_t cmd, uint8_t team);
static void traiterPaquetSonRadio(const SonPayload &p);
static void forwardSonToPc(uint16_t cmd, uint8_t team);
static void parseSerialLine(char *line);
static void radioSendCmdToBuzzers(uint8_t value);

// =========================================================
// CONFIGURATION
// =========================================================
RF24 radio(9, 10);

// Pipe "00002" : ordres son depuis la Mega (SonPayload).
// Pipe "00001" : relais RESET_ALL/RELANCE_PARTIEL vers les buzzers, depuis le PC (mode 3, Mega non branchée en série).
// Usage client : alimentation seule (pas de USB PC) — cérémonies sans ordinateur.
// Avec PC : la Mega envoie la même radio ; le logiciel peut jouer sur le PC si le nano est éteint (AUDIO_PC).
const byte adresseSon[6]     = "00002";
const byte adresseBuzzers[6] = "00001";

/** Même format que megaf.ino / nano_animateur (CMD 88 = relance, 99 = reset). */
struct RadioMsg {
    uint8_t kind;   // 2 = CMD
    uint8_t value;  // 88 / 99
    uint16_t seq;
};
static uint16_t seqOutboundCmd = 0;

#define RADIO_CHANNEL 108

bool radioOK = false;
bool mp3OK   = false;
unsigned long dernierBattement = 0;
unsigned long ledPulseUntil = 0;
/** Equipes 1..30 : bit i-1 = 1 si 01ii.mp3 a deja joue sans erreur (buzz plus rapide ensuite). */
static uint32_t teamTrackKnownOk = 0;

// ----- Port USB (PC, mode 3) : score + option AUDIO_PC (sons sur PC, DFPlayer muet pour eviter doublon) -----
#define SERIAL_BUF_LEN 80
static char serialBuf[SERIAL_BUF_LEN];
static uint8_t serialLen = 0;
/** Si true : ordres son recus par radio depuis la Mega sont renvoyes en USB (FWD_SON:...) sans jouer le DF. */
static bool audioViaPc = false;

/** Relaie RESET_ALL/RELANCE_PARTIEL vers les buzzers radio (pipe 00001), si la Mega n'est pas branchee en serie. */
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

/** Filtre bruit RF / taille paquet incorrecte (Mega = 4 octets, cmd 200/201/202). */
static bool sonPayloadValide(const SonPayload &p) {
    if (p.cmd != 200 && p.cmd != 201 && p.cmd != 202) return false;
    if (p.cmd == 200 && p.team > 30) return false;
    return true;
}

/** Dédoublonne retransmissions Mega (meme seq). */
static bool dedupeSonRadio(const SonPayload &p) {
    const unsigned long now = millis();
    static uint16_t dedupeCmd = 0xFFFFu;
    static uint8_t dedupeTeam = 0xFFu;
    static uint8_t dedupeSeq = 0xFFu;
    static unsigned long dedupeAtMs = 0;
    const unsigned long windowMs = (p.cmd == 200) ? 520UL : 420UL;
    if (p.cmd == dedupeCmd && p.team == dedupeTeam && p.seq == dedupeSeq &&
        (now - dedupeAtMs) < windowMs) {
        return true;
    }
    dedupeCmd = p.cmd;
    dedupeTeam = p.team;
    dedupeSeq = p.seq;
    dedupeAtMs = now;
    return false;
}

static bool sonRxQueuePush(const SonPayload &p) {
    uint8_t next = (uint8_t)((sonRxQTail + 1) % SON_RX_Q_LEN);
    if (next == sonRxQHead) {
        sonRxQHead = (uint8_t)((sonRxQHead + 1) % SON_RX_Q_LEN);
    }
    sonRxQ[sonRxQTail] = p;
    sonRxQTail = next;
    return true;
}

static bool sonRxQueuePop(SonPayload &out) {
    if (sonRxQHead == sonRxQTail) return false;
    out = sonRxQ[sonRxQHead];
    sonRxQHead = (uint8_t)((sonRxQHead + 1) % SON_RX_Q_LEN);
    return true;
}

static void ledPulseUpdate(void) {
    if (ledPulseUntil != 0 && (long)(millis() - ledPulseUntil) >= 0) {
        digitalWrite(LED_BUILTIN, LOW);
        ledPulseUntil = 0;
    }
}

static void ledFlashMs(uint8_t ms) {
    digitalWrite(LED_BUILTIN, HIGH);
    ledPulseUntil = millis() + (unsigned long)ms;
}

/** Mode AUDIO_PC : relaie l'ordre son recu par radio vers le PC au lieu de jouer sur le DF. */
static void forwardSonToPc(uint16_t cmd, uint8_t team) {
    if (cmd == 200) {
        Serial.print(F("FWD_SON:200:"));
        Serial.println((unsigned int)team);
    } else if (cmd == 201) {
        Serial.println(F("FWD_SON:201"));
    } else if (cmd == 202) {
        Serial.println(F("FWD_SON:202"));
    }
}

/** Traite un paquet radio SonPayload (un par appel — evite blocage DF trop long). */
static void traiterPaquetSonRadio(const SonPayload &p) {
    if (p.cmd != 200 && p.cmd != 201 && p.cmd != 202) return;
    if (audioViaPc) {
        forwardSonToPc(p.cmd, p.team);
    } else {
        playSonCmd(p.cmd, p.team);
    }
}

static void pollRadioRx(void) {
    if (!radioOK) return;
    SonPayload rx;
    while (radio.available()) {
        radio.read(&rx, sizeof(rx));
        if (!sonPayloadValide(rx)) continue;
        if (dedupeSonRadio(rx)) continue;
        sonRxQueuePush(rx);
    }
}

static void drainSonRxQueue(uint8_t maxPackets) {
    SonPayload p;
    for (uint8_t n = 0; n < maxPackets && sonRxQueuePop(p); n++) {
        traiterPaquetSonRadio(p);
    }
}

/** Exécute une commande son (200 buzz équipe, 201 victoire, 202 échec) — radio Mega ou PC (AUDIO_DF). */
static void playSonCmd(uint16_t cmd, uint8_t team) {
    if (!mp3OK) return;
    if (cmd != 200 && cmd != 201 && cmd != 202) return;

    ledFlashMs(25);

    /* Stop en cours : evite blocage DF si buzz puis valider/faux rapproches. */
    dfStopTrack(monLecteurMP3);
    drainDfSerial(monLecteurMP3, 12);

    if (cmd == 200) {
        jouerBuzzEquipeOuGenerique(monLecteurMP3, team);
    } else if (cmd == 201) {
        dfPlayTrack(monLecteurMP3, 2);
        drainDfSerial(monLecteurMP3, DF_DRAIN_CMD_MS);
    } else if (cmd == 202) {
        dfPlayTrack(monLecteurMP3, 3);
        drainDfSerial(monLecteurMP3, DF_DRAIN_CMD_MS);
    }
}

/** Commandes recues sur le port USB (PC, mode 3 : Mega non branchee en serie, tout passe par ce nano). */
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

    /* Remplacement Mega (PC non branché sur TTL) : mêmes lignes que le logiciel score → buzzers + son. */
    if (strcmp(line, "RESET_ALL") == 0) {
        radioSendCmdToBuzzers(99);
        Serial.println(F("CMD_SENT:RESET_ALL"));
        if (!audioViaPc) playSonCmd(201, 0);
        return;
    }
    if (strcmp(line, "RELANCE_PARTIEL") == 0) {
        radioSendCmdToBuzzers(88);
        Serial.println(F("CMD_SENT:RELANCE_PARTIEL"));
        if (!audioViaPc) playSonCmd(202, 0);
        return;
    }

    if (strcmp(line, "STOP") == 0) {
        if (audioViaPc || !mp3OK) return;
        dfStopTrack(monLecteurMP3);
        drainDfSerial(monLecteurMP3, DF_DRAIN_SHORT_MS);
        return;
    }

    /* SON:200:N / SON:201 / SON:202 — depuis le PC ; en AUDIO_PC le PC joue deja en pygame. */
    if (strncmp(line, "SON:", 4) == 0) {
        if (audioViaPc) return;
        unsigned long c = strtoul(line + 4, NULL, 10);
        if (c == 200) {
            char *colon = strchr(line + 4, ':');
            long team = colon ? strtol(colon + 1, NULL, 10) : 0;
            if (team < 0) team = 0;
            if (team > 255) team = 255;
            playSonCmd(200, (uint8_t)team);
        } else if (c == 201) {
            playSonCmd(201, 0);
        } else if (c == 202) {
            playSonCmd(202, 0);
        }
        return;
    }
}

/** Coupe RX le temps d'une commande DF puis re-ecoute (poll possible via drainDfSerial). */
static void dfRadioPauseTx(void) {
    if (!radioOK) return;
    radio.stopListening();
    delayMicroseconds(1200);
}

static void dfRadioResumeRx(void) {
    if (!radioOK) return;
    delayMicroseconds(1600);
    radio.startListening();
}

static void dfPlayTrack(DFRobotDFPlayerMini &df, uint8_t track) {
    dfRadioPauseTx();
    df.playMp3Folder((int)track);
    dfRadioResumeRx();
}

static void dfStopTrack(DFRobotDFPlayerMini &df) {
    dfRadioPauseTx();
    df.stop();
    dfRadioResumeRx();
}

/** Vide la file serie DF ; ecoute radio entre les polls. */
static void drainDfSerial(DFRobotDFPlayerMini &df, uint16_t totalMs) {
    const unsigned long t0 = millis();
    while (millis() - t0 < (unsigned long)totalMs) {
        pollRadioRx();
        drainSonRxQueue(1);
        while (df.available()) {
            (void)df.readType();
            (void)df.read();
        }
        delay(2);
    }
}

/** True si le DF signale fichier/piste absent (timeout = on suppose OK). */
static bool dfPollFileMissing(DFRobotDFPlayerMini &df, uint16_t maxMs) {
    const unsigned long t0 = millis();
    while (millis() - t0 < (unsigned long)maxMs) {
        pollRadioRx();
        drainSonRxQueue(1);
        while (df.available()) {
            const uint8_t t = df.readType();
            const uint16_t par = df.read();
            if (t == DFPlayerError && dfErreurPisteAbsent(par)) {
                return true;
            }
        }
        delay(2);
    }
    return false;
}

/** Vrai « fichier / piste absent » DFPlayer (pas tout octet lu comme erreur sur bus bruite). */
static bool dfErreurPisteAbsent(uint16_t par) {
    uint8_t lo = (uint8_t)(par & 0xFFu);
    uint8_t hi = (uint8_t)((par >> 8) & 0xFFu);
    return lo == 0x06u || lo == 0x09u || hi == 0x06u || hi == 0x09u;
}

/** Son buzz : 01NN.mp3 ; repli 0001 si absent. Pistes deja OK = lecture immediate. */
static void jouerBuzzEquipeOuGenerique(DFRobotDFPlayerMini &df, uint8_t team) {
    if (team < 1 || team > 30) {
        dfPlayTrack(df, 1);
        drainDfSerial(df, DF_DRAIN_SHORT_MS);
        return;
    }
    const uint8_t track = (uint8_t)(100 + team);
    const uint32_t bit = (1UL << (team - 1));

    if (teamTrackKnownOk & bit) {
        dfPlayTrack(df, track);
        drainDfSerial(df, DF_DRAIN_SHORT_MS);
        return;
    }

    dfPlayTrack(df, track);
    if (dfPollFileMissing(df, DF_ERR_POLL_MS)) {
        dfStopTrack(df);
        delayMicroseconds(2500);
        dfPlayTrack(df, 1);
        drainDfSerial(df, DF_DRAIN_SHORT_MS);
        return;
    }

    teamTrackKnownOk |= bit;
    drainDfSerial(df, DF_DRAIN_SHORT_MS);
}

/** True si la piste semble absente (erreur DF rapide). */
static bool dfProbeTrackMissing(DFRobotDFPlayerMini &df, uint8_t track, uint16_t maxMs) {
    dfPlayTrack(df, track);
    const bool missing = dfPollFileMissing(df, maxMs);
    dfStopTrack(df);
    drainDfSerial(df, 10);
    return missing;
}

/** Au boot : remplit teamTrackKnownOk pour buzz immediat (evite ~100 ms par equipe). */
static void sonProbeSdAtBoot(DFRobotDFPlayerMini &df) {
    df.volume(0);
    digitalWrite(LED_BUILTIN, HIGH);
    for (uint8_t team = 1; team <= 30; team++) {
        if (!dfProbeTrackMissing(df, (uint8_t)(100 + team), DF_PROBE_BOOT_MS)) {
            teamTrackKnownOk |= (1UL << (team - 1));
        }
        if ((team % 10) == 0) {
            ledFlashMs(40);
            ledPulseUpdate();
        }
    }
    (void)dfProbeTrackMissing(df, 1, DF_PROBE_BOOT_MS);
    (void)dfProbeTrackMissing(df, 2, DF_PROBE_BOOT_MS);
    (void)dfProbeTrackMissing(df, 3, DF_PROBE_BOOT_MS);
    df.volume(DF_VOLUME);
    digitalWrite(LED_BUILTIN, LOW);
}

// =========================================================
// SETUP
// =========================================================
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.begin(9600);

    // 1. DFPlayer alimenté en 3.3V via SoftwareSerial
    mySoftwareSerial.begin(9600);
    delay(300);
    /* isACK=true : commandes DF plus fiables sur le bus SoftwareSerial. */
    if (monLecteurMP3.begin(mySoftwareSerial, false, true)) {
        mp3OK = true;
        monLecteurMP3.setTimeOut(300);
        monLecteurMP3.volume(DF_VOLUME);
        monLecteurMP3.EQ(DFPLAYER_EQ_NORMAL);
        monLecteurMP3.outputDevice(DFPLAYER_DEVICE_SD);
        drainDfSerial(monLecteurMP3, 80);
        sonProbeSdAtBoot(monLecteurMP3);
    }

    // 2. RADIO — aligné sur megaf (pipe 00002, paquet 4 octets)
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

/** Lit le port USB (PC) sans bloquer, ligne par ligne. */
static void pollSerialPc(void) {
    for (uint16_t n = 0; n < 128 && Serial.available() > 0; n++) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            serialBuf[serialLen] = '\0';
            while (serialLen > 0 && (serialBuf[serialLen - 1] == ' ' || serialBuf[serialLen - 1] == '\t')) {
                serialBuf[--serialLen] = '\0';
            }
            if (serialLen > 0) parseSerialLine(serialBuf);
            serialLen = 0;
        } else if (serialLen < SERIAL_BUF_LEN - 1) {
            serialBuf[serialLen++] = c;
        } else {
            serialLen = 0;
        }
    }
}

// =========================================================
// LOOP
// =========================================================
void loop() {
    const unsigned long now = millis();

    ledPulseUpdate();
    pollSerialPc();

    if (radioOK) {
        static unsigned long rearmEcouteMs = 0;
        if (now - rearmEcouteMs > SON_REARM_LISTEN_MS) {
            rearmEcouteMs = now;
            radio.startListening();
        }
        pollRadioRx();
        drainSonRxQueue(4);
    }

    if (radioOK && now - dernierBattement > 2000) {
        ledFlashMs(20);
        dernierBattement = now;
    }
}
