
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// 1 = ne pas appeler begin() du DFPlayer (moniteur série + radio seulement)
#ifndef TEST_RX_SKIP_DFPLAYER
#define TEST_RX_SKIP_DFPLAYER 0
#endif

RF24 radio(9, 10);

const byte adresseSon[6] = "00002";
#define RADIO_CHANNEL 108

SoftwareSerial mySoftwareSerial(5, 4); // RX=D5, TX=D4
DFRobotDFPlayerMini monLecteurMP3;

struct SonPayload {
    uint16_t cmd;
    uint8_t team;
    uint8_t reserved;
};

bool radioOK = false;
bool mp3OK = false;
unsigned long dernierBattement = 0;
static unsigned long dernierPaquetValideMs = 0;
static unsigned long dernierHintSerieMs = 0;

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

void setup() {
    Serial.begin(9600);
    pinMode(LED_BUILTIN, OUTPUT);
    delay(300);
    Serial.println();
    Serial.println(F("=== Nano TEST : demarrage (serie OK) ==="));

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

    Serial.println(F("Radio OK — pipe 00002, canal 108, paquet 4 octets"));
    Serial.flush();

#if TEST_RX_SKIP_DFPLAYER
    Serial.println(F("TEST_RX_SKIP_DFPLAYER=1 — DFPlayer ignore"));
    mp3OK = false;
#else
    Serial.println(F("Init DFPlayer (quelques secondes si module HS ou absent)..."));
    Serial.flush();
    delay(50);

    dfPauseRadioInit();
    mySoftwareSerial.begin(9600);
    delay(200);

    if (monLecteurMP3.begin(mySoftwareSerial, false, false)) {
        mp3OK = true;
        monLecteurMP3.setTimeOut(500);
        monLecteurMP3.volume(25);
        monLecteurMP3.EQ(DFPLAYER_EQ_NORMAL);
        Serial.println(F("DFPlayer OK"));
    } else {
        mp3OK = false;
        Serial.println(F("DFPlayer non detecte — traces serie + radio, pas de MP3"));
    }
    dfResumeRadioInit();
#endif

    Serial.println(F("Pret — en attente de paquets (Mega test TX)... Moniteur 9600 baud."));
    Serial.flush();

    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        delay(100);
    }

    /* Ne pas vider la FIFO RX ici : risque de perdre le premier 200 (BUZZ) deja recu. */
    if (radioOK) {
        delay(5);
        radio.startListening();
    }
    dernierPaquetValideMs = millis();
    dernierHintSerieMs = dernierPaquetValideMs;
}

void loop() {
    const unsigned long now = millis();

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
        if (now - dernierPaquetValideMs > 30000UL && now - dernierHintSerieMs > 30000UL) {
            dernierHintSerieMs = now;
            Serial.println(F("> 30s sans paquet valide — Mega TX ? 9600 baud ? canal 108 pipe 00002"));
            Serial.flush();
        }
        return;
    }

    SonPayload p = {0, 0, 0};
    radio.read(&p, sizeof(p));

    if (p.cmd != 200 && p.cmd != 201 && p.cmd != 202) {
        return;
    }

    /* Dedupe seulement 201/202 (double TX Mega) — pas le buzz 200. */
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
        Serial.println(F("BUZZ (play MP3)"));
    } else if (p.cmd == 201) {
        Serial.println(F("VICTOIRE (play 2)"));
    } else if (p.cmd == 202) {
        Serial.println(F("ECHEC (play 3)"));
    }
    Serial.flush();

    jouerSelonPayload(p);
}
