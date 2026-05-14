/*
 * TEST NANO — DFPlayer + radio (aligné sur mega_test_son_tx / nano_son_final)
 *
 * 1) Init radio (nRF24 CE=9 CSN=10, pipe "00002", canal 108, paquet SonPayload 4 octets)
 * 2) Init DFPlayer (RX=D5, TX=D4) — begin() a 2 arguments comme ton test DF seul
 * 3) Option : jouer 1-2-3 en local au demarrage pour verifier la SD
 * 4) loop : ecoute la Mega (mega_test_son_tx) et joue 200 / 201 / 202 comme nano_son_final
 *
 * RUN_STARTUP_MP3_SEQUENCE = 1 : apres DF OK, enchaine 0001 / 0002 / 0003 puis ecoute radio
 * RUN_STARTUP_MP3_SEQUENCE = 0 : passe directement a l'ecoute (test rapide avec Mega TX)
 */
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

#ifndef RUN_STARTUP_MP3_SEQUENCE
#define RUN_STARTUP_MP3_SEQUENCE 1
#endif

#ifndef TEST_DFPLAYER_SKIP_INIT
#define TEST_DFPLAYER_SKIP_INIT 0
#endif

RF24 radio(9, 10);
const byte adresseSon[6] = "00002";
#define RADIO_CHANNEL 108

SoftwareSerial mySoftwareSerial(5, 4);
DFRobotDFPlayerMini monLecteurMP3;

struct SonPayload {
    uint16_t cmd;
    uint8_t team;
    uint8_t reserved;
};

bool radioOK = false;
bool mp3OK = false;
unsigned long dernierBattement = 0;
/** Dernier paquet accepte (200/201/202) — rappel serie si silence prolonge. */
static unsigned long dernierPaquetValideMs = 0;
static unsigned long dernierHintSerieMs = 0;

/* SPI nRF24 + SoftwareSerial : couper l'ecoute SEULEMENT pendant l'envoi d'une commande DF
 * (play/stop). Si on reste en stopListening pendant buzz + drains, la Mega peut envoyer 201/202
 * pendant ce temps : paquets perdus -> serie "victoire echec echec" sans BUZZ, etc. */
static void dfPlayTrack(DFRobotDFPlayerMini &df, uint8_t track) {
    if (radioOK) {
        radio.stopListening();
        delay(3);
    }
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

/** Init DF : pause radio pendant begin (une fois au demarrage). */
static void dfPauseRadioInit() {
    if (radioOK) {
        radio.stopListening();
        delay(3);
    }
}

static void dfResumeRadioInit() {
    if (radioOK) {
        delay(2);
        radio.startListening();
    }
}

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

static bool dfErreurPisteAbsent(uint16_t par) {
    uint8_t lo = (uint8_t)(par & 0xFFu);
    uint8_t hi = (uint8_t)((par >> 8) & 0xFFu);
    return lo == 0x06u || lo == 0x09u || hi == 0x06u || hi == 0x09u;
}

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

void jouerSelonPayload(const SonPayload &p) {
    if (!mp3OK) {
        return;
    }
    if (p.cmd == 200) {
        jouerBuzzEquipeOuGenerique(monLecteurMP3, p.team);
    } else if (p.cmd == 201) {
        dfPlayTrack(monLecteurMP3, 2);
        drainDfSerial(monLecteurMP3, 80);
    } else if (p.cmd == 202) {
        dfPlayTrack(monLecteurMP3, 3);
        drainDfSerial(monLecteurMP3, 80);
    }
}

static void initRadio() {
    if (!radio.begin()) {
        Serial.println(F("ERREUR: nRF24 non detecte"));
        while (true) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(300);
            digitalWrite(LED_BUILTIN, LOW);
            delay(300);
        }
    }
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
    Serial.println(F("Radio OK — ecoute Mega (pipe 00002, canal 108)"));
}

static void initDfplayer() {
#if TEST_DFPLAYER_SKIP_INIT
    mp3OK = false;
    Serial.println(F("TEST_DFPLAYER_SKIP_INIT=1 — pas de DFPlayer"));
    return;
#endif
    dfPauseRadioInit();
    Serial.println(F("Init DFPlayer..."));
    Serial.flush();
    delay(50);
    mySoftwareSerial.begin(9600);
    delay(200);

    if (monLecteurMP3.begin(mySoftwareSerial, false)) {
        mp3OK = true;
        monLecteurMP3.setTimeOut(500);
        monLecteurMP3.volume(25);
        monLecteurMP3.EQ(DFPLAYER_EQ_NORMAL);
        Serial.println(F("DFPlayer OK !"));
    } else {
        mp3OK = false;
        Serial.println(F("ERREUR : DFPlayer non detecte — radio + serie seulement"));
    }
    dfResumeRadioInit();
}

#if RUN_STARTUP_MP3_SEQUENCE && !TEST_DFPLAYER_SKIP_INIT
static void sequenceMp3Locale() {
    delay(1000);

    Serial.println(F("Test local 1 : BUZZ (0001.mp3)..."));
    digitalWrite(LED_BUILTIN, HIGH);
    dfPlayTrack(monLecteurMP3, 1);
    delay(3000);
    digitalWrite(LED_BUILTIN, LOW);
    delay(2000);

    Serial.println(F("Test local 2 : VICTOIRE (0002.mp3)..."));
    digitalWrite(LED_BUILTIN, HIGH);
    dfPlayTrack(monLecteurMP3, 2);
    delay(3000);
    digitalWrite(LED_BUILTIN, LOW);
    delay(2000);

    Serial.println(F("Test local 3 : ECHEC (0003.mp3)..."));
    digitalWrite(LED_BUILTIN, HIGH);
    dfPlayTrack(monLecteurMP3, 3);
    delay(3000);
    digitalWrite(LED_BUILTIN, LOW);
    delay(2000);

    Serial.println(F("Sequence locale terminee — ecoute radio Mega..."));
}
#endif

void setup() {
    Serial.begin(9600);
    pinMode(LED_BUILTIN, OUTPUT);
    delay(300);
    Serial.println();
    Serial.println(F("=== TEST Nano DFPlayer + radio (mega_test_son_tx) ==="));

    initRadio();
    Serial.flush();

    initDfplayer();
    Serial.flush();

#if RUN_STARTUP_MP3_SEQUENCE && !TEST_DFPLAYER_SKIP_INIT
    if (mp3OK) {
        sequenceMp3Locale();
    }
#endif

    Serial.println(F("Pret — paquets 200 / 201 / 202 depuis la Mega (cycle ~11,5 s). Moniteur 9600 baud."));
    Serial.flush();

    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        delay(100);
    }

    /* Ne pas vider la FIFO RX ici : la Mega peut deja avoir envoye 200 — sinon le premier
     * BUZZ de chaque cycle est perdu (il ne reste que 201 / 202 au moniteur). */
    if (radioOK) {
        delay(5);
        radio.startListening();
    }
    dernierPaquetValideMs = millis();
    dernierHintSerieMs = dernierPaquetValideMs;
}

void loop() {
    const unsigned long now = millis();

    /* Re-arm ecoute PRX regulierement : apres SPI/SoftSerial le module peut rester coince
     * hors reception -> moniteur bloque sur "Pret" sans RECU. */
    if (radioOK) {
        static unsigned long rearmEcouteMs = 0;
        if (now - rearmEcouteMs > 2500UL) {
            rearmEcouteMs = now;
            radio.startListening();
        }
    }

    if (radioOK && now - dernierBattement > 2000UL) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
        digitalWrite(LED_BUILTIN, LOW);
        dernierBattement = now;
    }

    if (!radioOK) {
        return;
    }

    if (!radio.available()) {
        /* Rappel serie : moniteur 9600 baud ; Mega test TX branchée, meme canal / adresse. */
        if (now - dernierPaquetValideMs > 30000UL && now - dernierHintSerieMs > 30000UL) {
            dernierHintSerieMs = now;
            Serial.println(F("> 30s sans paquet valide — Mega TX allumee ? 9600 baud ? canal 108 pipe 00002"));
            Serial.flush();
        }
        return;
    }

    /* Un seul paquet par loop : evite d'enfiler 200+201+202 puis bloquer longtemps en MP3
     * pendant que d'autres paquets arrivent (FIFO 3 niveaux -> pertes / desordre). */
    SonPayload p = {0, 0, 0};
    radio.read(&p, sizeof(p));

    if (p.cmd != 200 && p.cmd != 201 && p.cmd != 202) {
        return;
    }

    /* Dedupe seulement 201/202 (double TX Mega) — pas le 200 (buzz envoye une fois). */
    static uint16_t dedupeCmd = 0xFFFFu;
    static uint8_t dedupeTeam = 0xFFu;
    static unsigned long dedupeAtMs = 0;
    if ((p.cmd == 201 || p.cmd == 202) && p.cmd == dedupeCmd && p.team == dedupeTeam &&
        (now - dedupeAtMs) < 160UL) {
        dernierPaquetValideMs = now;
        return;
    }
    if (p.cmd == 201 || p.cmd == 202) {
        dedupeCmd = (uint16_t)p.cmd;
        dedupeTeam = p.team;
        dedupeAtMs = now;
    }

    dernierPaquetValideMs = now;

    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
    digitalWrite(LED_BUILTIN, LOW);

    Serial.print(F("RECU cmd="));
    Serial.print(p.cmd);
    Serial.print(F(" team="));
    Serial.print(p.team);
    Serial.print(F(" -> "));
    if (p.cmd == 200) {
        Serial.println(F("BUZZ"));
    } else if (p.cmd == 201) {
        Serial.println(F("VICTOIRE"));
    } else if (p.cmd == 202) {
        Serial.println(F("ECHEC"));
    }
    Serial.flush();

    jouerSelonPayload(p);
}
