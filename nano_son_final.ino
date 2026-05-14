#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// =========================================================
// CONFIGURATION
// =========================================================
RF24 radio(9, 10);

// Adresse dédiée Nano son
// Reçoit UNIQUEMENT les signaux de la Mega !
// Jamais les signaux des buzzers équipes !
const byte adresseSon[6] = "00002";

// DFPlayer sur broches D4 (TX) et D5 (RX)
SoftwareSerial mySoftwareSerial(5, 4); // RX=D5, TX=D4
DFRobotDFPlayerMini monLecteurMP3;

// =========================================================
// FICHIERS MP3 SUR CARTE SD (dossier /mp3/ sur carte microSD DFPlayer)
// =========================================================
// mp3/0001.mp3 → Son BUZZ générique / repli si pas de son équipe
// mp3/0002.mp3 → Son VICTOIRE (bonne réponse)
// mp3/0003.mp3 → Son ÉCHEC (mauvaise réponse)
// mp3/0101.mp3 → BUZZ équipe 1 (optionnel ; sinon le module renvoie une erreur → lecture 0001)
// mp3/0102.mp3 → BUZZ équipe 2 … jusqu’à 0130 équipe 30

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

/** SPI nRF24 vs SoftwareSerial : pause radio seulement autour play/stop (pas tout le buzz). */
static void dfPlayTrack(DFRobotDFPlayerMini &df, uint8_t track) {
    if (radioOK) {
        radio.stopListening();
        delay(3);
    }
    df.play(track);
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
        radio.setChannel(108);
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
}

// =========================================================
// LOOP
// =========================================================
void loop() {
    const unsigned long now = millis();

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

    // RÉCEPTION RADIO (uniquement depuis la Mega)
    if (radioOK && radio.available()) {
        SonPayload p = {0, 0, 0};
        radio.read(&p, sizeof(p));

        if (p.cmd != 200 && p.cmd != 201 && p.cmd != 202) {
            return;
        }

        /* Double emission Mega seulement pour 201/202 — ne pas dedoublonner le buzz 200. */
        static uint16_t dedupeCmd = 0xFFFFu;
        static uint8_t dedupeTeam = 0xFFu;
        static unsigned long dedupeAtMs = 0;
        if ((p.cmd == 201 || p.cmd == 202) && p.cmd == dedupeCmd && p.team == dedupeTeam &&
            (now - dedupeAtMs) < 160UL) {
            return;
        }
        if (p.cmd == 201 || p.cmd == 202) {
            dedupeCmd = (uint16_t)p.cmd;
            dedupeTeam = p.team;
            dedupeAtMs = now;
        }

        // Clignotement = signal reçu
        digitalWrite(LED_BUILTIN, HIGH); delay(50);
        digitalWrite(LED_BUILTIN, LOW);

        if (!mp3OK) return;

        // =====================================================
        // 200 = Buzz valide → son d’équipe (0101..0130) ou 0001 par défaut
        // =====================================================
        if (p.cmd == 200) {
            jouerBuzzEquipeOuGenerique(monLecteurMP3, p.team);
        }

        // =====================================================
        // 201 = Bonne réponse → 0002.mp3
        // =====================================================
        else if (p.cmd == 201) {
            dfPlayTrack(monLecteurMP3, 2);
            drainDfSerial(monLecteurMP3, 80);
        }

        // =====================================================
        // 202 = Mauvaise réponse → 0003.mp3
        // =====================================================
        else if (p.cmd == 202) {
            dfPlayTrack(monLecteurMP3, 3);
            drainDfSerial(monLecteurMP3, 80);
        }
    }
}
