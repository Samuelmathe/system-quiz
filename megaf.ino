#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <DMXSerial.h>
#include <EEPROM.h>
#include <string.h>
#include <stdlib.h>
#include <avr/wdt.h> // [FIX] watchdog matériel pour éviter un blocage définitif

// =========================================================
// CONFIGURATION MATÉRIELLE
// =========================================================
RF24 radio(9, 53);

const byte adresseBuzzers[6] = "00001";
const byte adresseSon[6]     = "00002";

#define RADIO_CHANNEL  108
#define LED 13
#define MAGIC_NUMBER 0xAB
#define PC_SERIAL Serial3

// =========================================================
// STRUCTURE EEPROM
// =========================================================
struct QuizConfig {
    byte magic;
    int nbEquipes;
    int nbCanaux;
    int offDim, offR, offG, offB;
    int adressesDMX[30];
    byte couleurs[30][30][3];
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
unsigned long dernierAliveMs   = 0; // [FIX] journal de vie périodique sur PC_SERIAL

// [FIX] Auto-surveillance radio : détecte une puce nRF24 qui a décroché
// (mais qui ne bloque pas le SPI) et tente une réinitialisation.
// ATTENTION : ceci ne protège PAS contre un blocage SPI dur en plein
// milieu d'un radio.write()/available() — c'est le rôle du watchdog.
unsigned long dernierCheckRadio = 0;
#define RADIO_CHECK_INTERVAL_MS 5000

// =========================================================
// RADIO STRUCTURES
// =========================================================
struct RadioMsg {
    uint8_t kind;   // 1=BUZZ_EQUIPE, 2=CMD
    uint8_t value;  // BUZZ: team 1..30 ; CMD: 88/99
    uint16_t seq;
};
static uint16_t lastSeqEquipe[30] = {0};
static uint16_t lastSeqCmd = 0;
static bool lastSeqInitEquipe[30] = {false};
static bool lastSeqInitCmd = false;
static uint16_t seqOutboundCmd = 0;

struct SonPayload {
    uint16_t cmd;
    uint8_t team;
    uint8_t seq;
};
static uint8_t sonSeqCounter = 0;

// MACHINE D'ÉTAT POUR L'ENVOI DU SON SANS DELAY()
bool sonEnvoiActif = false;
SonPayload sonQueuePayload;
uint8_t sonPhase = 0;
uint8_t sonBurstIdx = 0;
unsigned long sonProchainEnvoiMs = 0;
uint8_t sonBurstN = 0;
uint8_t sonGapMs = 0;
uint8_t sonPauseMs = 0;

static void radioSendCmdToBuzzers(uint8_t value) {
    RadioMsg msg = {2, value, ++seqOutboundCmd};
    radio.stopListening();
    radio.openWritingPipe(adresseBuzzers);
    radio.write(&msg, sizeof(msg));
    radio.openWritingPipe(adresseBuzzers); // Sécurité ré-écriture
    radio.startListening();
}

// [FIX] Réinitialise complètement la radio avec la même config que setup().
// Appelée par verifierEtReanimerRadio() si la puce ne répond plus.
static void reinitialiserRadio() {
    if (radio.begin()) {
        radioOK = true;
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
        sonEnvoiActif = false; // on annule un éventuel envoi son resté en suspens
        PC_SERIAL.println("INFO:RADIO_REINIT_OK");
    } else {
        radioOK = false;
        PC_SERIAL.println("ERR:RADIO_REINIT_FAILED");
    }
}

// [FIX] Vérification périodique et non bloquante de l'état de la puce radio.
static void verifierEtReanimerRadio() {
    if (!radioOK) return;
    unsigned long now = millis();
    if (now - dernierCheckRadio < RADIO_CHECK_INTERVAL_MS) return;
    dernierCheckRadio = now;

    if (!radio.isChipConnected()) {
        PC_SERIAL.println("WARN:RADIO_LOST_REINIT");
        reinitialiserRadio();
    }
}

// ---- LED non-bloquante ----
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

// ---- Buffer ligne série PC ----
#define SERIAL_BUF_LEN 96
char serialBuf[SERIAL_BUF_LEN];
uint8_t serialLen = 0;

void setProjecteur(int addr, int r, int g, int b);
void eteindreLumieres();
void flashDmxUpdate();
void flashDmxStartVert();
void flashDmxStartRouge();

// ---- Clignotement DMX validation/refus non-bloquant ----
#define FLASH_DMX_HALF_MS 150
#define FLASH_DMX_STEPS   6
static uint8_t flashDmxMode = 0;
static uint8_t flashDmxStep = 0;
static unsigned long flashDmxNextMs = 0;

void flashDmxStartVert() { flashDmxMode = 1; flashDmxStep = 0; flashDmxNextMs = millis(); }
void flashDmxStartRouge() { flashDmxMode = 2; flashDmxStep = 0; flashDmxNextMs = millis(); }

void flashDmxUpdate() {
    if (flashDmxMode == 0) return;
    unsigned long now = millis();
    if ((long)(now - flashDmxNextMs) < 0) return;

    bool vert = (flashDmxMode == 1);
    if (flashDmxStep % 2 == 0) {
        eteindreLumieres();
        for (int p = 0; p < 30; p++) {
            int addr = settings.adressesDMX[p];
            if (addr > 0 && addr <= 512) {
                if (vert) setProjecteur(addr, 0, 255, 0);
                else       setProjecteur(addr, 255, 0, 0);
            }
        }
    } else {
        eteindreLumieres();
    }
    flashDmxStep++;
    if (flashDmxStep >= FLASH_DMX_STEPS) {
        flashDmxMode = 0;
        flashDmxStep = 0;
        eteindreLumieres();
    } else {
        flashDmxNextMs = now + FLASH_DMX_HALF_MS;
    }
}

// ---- BUZZ SIMULTANÉS - Fenêtre 50ms ----
#define FENETRE_MS   50
#define MAX_BUFFER    8
int  bufferSignaux[MAX_BUFFER];
int  nbBuffer        = 0;
bool fenetreActive   = false;
unsigned long debutFenetre = 0;

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
        {255,   0,   0}, {0,   0, 255}, {0, 255,   0}, {255, 255,   0},
        {255,   0, 128}, {0, 255, 255}, {0, 255,   0}, {0,   0, 255}
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
    settings.couleurs[6][1][0] = 255; settings.couleurs[6][1][1] = 0; settings.couleurs[6][1][2] = 128;
    settings.couleurs[7][1][0] = 0; settings.couleurs[7][1][1] = 0; settings.couleurs[7][1][2] = 255;
    EEPROM.put(0, settings); // Sûr ici : appelé uniquement depuis setup(), avant wdt_enable()
}

// Initialise la structure de timing d'envoi du son sans bloquer le microcontrôleur
void envoyerSon(uint16_t cmd, uint8_t team) {
    if (!radioOK) return;

    // [FIX] Note explicite : si un envoi son est déjà en cours (sonEnvoiActif == true),
    // cet appel écrase silencieusement la séquence en cours par la nouvelle.
    // C'est le comportement voulu (ex: RESET_ALL doit couper le son du buzz
    // précédent plutôt que d'attendre la fin de la salve), documenté ici pour
    // éviter toute confusion lors d'une future modification.
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

// Gérée de manière asynchrone dans la loop
void updateRadioSonAsynchrone() {
    if (!sonEnvoiActif) return;

    unsigned long now = millis();
    if ((long)(now - sonProchainEnvoiMs) < 0) return;

    // Écriture brute non bloquante vers le Nano Son
    for (uint8_t retry = 0; retry < 4; retry++) { // Réduit à 4 essais rapides
        if (radio.write(&sonQueuePayload, sizeof(sonQueuePayload), true)) break;
        delayMicroseconds(300);
    }

    sonBurstIdx++;
    if (sonBurstIdx < sonBurstN) {
        sonProchainEnvoiMs = now + sonGapMs;
    } else {
        // Fin du premier burst
        if (sonPhase == 0) {
            sonPhase = 1;
            sonBurstIdx = 0;
            sonProchainEnvoiMs = now + sonPauseMs;
        } else {
            // Fin de la double salve globale -> Restauration du mode écoute Buzzers
            sonEnvoiActif = false;
            radio.setAutoAck(true);
            radio.openWritingPipe(adresseBuzzers);
            radio.startListening();
        }
    }
}

// =========================================================
// SETUP
// =========================================================
void setup() {
    wdt_disable(); // [FIX] désactive un éventuel watchdog résiduel avant tout le reste

    pinMode(LED, OUTPUT);
    DMXSerial.init(DMXController);
    eteindreLumieres();

    EEPROM.get(0, settings);
    if (settings.magic != MAGIC_NUMBER ||
        settings.nbEquipes < 1 || settings.nbEquipes > 30 ||
        settings.nbCanaux  < 1 || settings.nbCanaux  > 30) {
        initialiserConfigParDefaut(); // peut prendre plusieurs secondes, sûr ici (watchdog pas encore actif)
    }

    PC_SERIAL.begin(9600);

    if (radio.begin()) {
        radioOK = true;
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

        for (int i = 0; i < 3; i++) {
            digitalWrite(LED, HIGH); delay(60);
            digitalWrite(LED, LOW);  delay(60);
        }
    } else {
        radioOK = false;
        digitalWrite(LED, HIGH); delay(1000); digitalWrite(LED, LOW);
    }

    RadioMsg poubelle;
    while (radio.available()) { radio.read(&poubelle, sizeof(poubelle)); }
    eteindreLumieres();

    wdt_enable(WDTO_4S); // [FIX] reset matériel automatique si loop() ne revient pas sous 4s
}

// =========================================================
// LOOP PRINCIPALE
// =========================================================
void loop() {
    wdt_reset(); // [FIX] doit être en tout début de loop() : "je suis vivant"

    ledUpdate();
    flashDmxUpdate();
    updateRadioSonAsynchrone(); // Traitement du son en arrière-plan (0% de blocage)
    verifierEtReanimerRadio();  // [FIX] auto-surveillance radio (non bloquant)

    // Traiter fenêtre expirée (50ms)
    if (fenetreActive && millis() - debutFenetre >= FENETRE_MS) {
        bool traite = false; // [FIX] pour diagnostic
        for (int i = 0; i < nbBuffer; i++) {
            int sig = bufferSignaux[i];
            if (sig >= 1 && sig <= settings.nbEquipes && !dejaJoue[sig] && !jeuVerrouille) {
                jeuVerrouille = true;
                dejaJoue[sig] = true;
                allumerCouleurEquipe(sig);
                PC_SERIAL.print("BUZZ:"); PC_SERIAL.println(sig);
                envoyerSon(200, (uint8_t)sig);
                traite = true;
                break;
            }
        }
        if (!traite && nbBuffer > 0) {
            // [FIX] Diagnostic : des buzz sont arrivés mais aucun n'a été traité.
            // locked=1 -> jeu verrouillé (pas de RESET_ALL/RELANCE_PARTIEL envoyé entre les manches)
            // locked=0 -> équipe(s) déjà jouée(s) ce tour (dejaJoue) ou hors plage
            PC_SERIAL.print("IGNORED:locked="); PC_SERIAL.print(jeuVerrouille ? 1 : 0);
            PC_SERIAL.print(",count="); PC_SERIAL.println(nbBuffer);
        }
        fenetreActive = false;
        nbBuffer = 0;
        RadioMsg p;
        while (radio.available()) { radio.read(&p, sizeof(p)); } // Nettoyage complet du buffer
    }

    // BATTEMENT LED (Non-bloquant)
    if (radioOK && millis() - dernierBattement > 2000) {
        ledPulse(40);
        dernierBattement = millis();
    }

    // [FIX] Journal de vie périodique sur PC_SERIAL, indépendant de radioOK :
    // preuve horodatée en cas d'incident. Si ces lignes s'arrêtent net -> freeze confirmé.
    // Si elles continuent avec locked=1 pendant que les équipes buzzent -> jeu verrouillé, pas un freeze.
    if (millis() - dernierAliveMs > 2000) {
        dernierAliveMs = millis();
        PC_SERIAL.print("ALIVE:"); PC_SERIAL.print(millis());
        PC_SERIAL.print(",radioOK="); PC_SERIAL.print(radioOK ? 1 : 0);
        PC_SERIAL.print(",locked="); PC_SERIAL.println(jeuVerrouille ? 1 : 0);
    }

    // Reset anti-doublon après 1 seconde
    if (dernierSignal != -1 && millis() - dernierSignalTemps > 1000) {
        dernierSignal = -1;
    }

    // RÉCEPTION série PC - Non bloquant
    while (PC_SERIAL.available() > 0) { // Utilisation d'un while fluide pour purger le flux PC
        char c = (char)PC_SERIAL.read();
        if (c == '\r') continue;
        if (c == '\n') {
            serialBuf[serialLen] = '\0';
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
                serialLen = 0;
            }
        }
    }

    // RÉCEPTION RADIO buzzers
    // [FIX] "while" au lieu de "if" : vide complètement le tampon matériel (jusqu'à 3
    // paquets côté nRF24) à chaque tour de boucle, au lieu de n'en traiter qu'un seul.
    // Avec plusieurs équipes qui buzzent en rafale, un simple "if" pouvait laisser des
    // paquets s'accumuler et saturer le tampon, provoquant la perte de tous les buzz
    // suivants tant que loop() ne repassait pas par ce point.
    while (!sonEnvoiActif && radio.available()) {
        RadioMsg msg = {0, 0, 0};
        radio.read(&msg, sizeof(msg));
        int signal = (int)msg.value;

        bool duplicate = false;
        if (msg.kind == 1) {
            int idx = signal - 1;
            if (idx >= 0 && idx < settings.nbEquipes) {
                if (lastSeqInitEquipe[idx] && lastSeqEquipe[idx] == msg.seq) {
                    duplicate = true;
                } else {
                    lastSeqEquipe[idx] = msg.seq;
                    lastSeqInitEquipe[idx] = true;
                }
            }
        } else if (msg.kind == 2) {
            if (lastSeqInitCmd && lastSeqCmd == msg.seq) {
                duplicate = true;
            } else {
                lastSeqCmd = msg.seq;
                lastSeqInitCmd = true;
            }
        } else {
            if (signal == dernierSignal) { duplicate = true; }
        }

        if (!duplicate) {
            dernierSignal      = signal;
            dernierSignalTemps = millis();
            ledPulse(60);

            // C'est une équipe -> traitement fenêtre simultanée
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

            // 99 = RESET_ALL
            else if (msg.kind == 2 && signal == 99) {
                PC_SERIAL.println("CMD_SENT:RESET_ALL");
                envoyerSon(201, 0);
                for (int i = 0; i < 31; i++) dejaJoue[i] = false;
                jeuVerrouille = false;
                dernierSignal = -1;
                flashDmxStartVert();
                ledPulse(150);
                while (radio.available()) { RadioMsg poubelle; radio.read(&poubelle, sizeof(poubelle)); }
            }

            // 88 = RELANCE_PARTIEL
            else if (msg.kind == 2 && signal == 88) {
                PC_SERIAL.println("CMD_SENT:RELANCE_PARTIEL");
                envoyerSon(202, 0);
                jeuVerrouille = false;
                dernierSignal = -1;
                flashDmxStartRouge();
                ledPulse(150);
                while (radio.available()) { RadioMsg poubelle; radio.read(&poubelle, sizeof(poubelle)); }
            }
        }
    }
}

// =========================================================
// PARSING COMMANDES (PC via PC_SERIAL)
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
        if (*p == ':') p++;
        const char *endp = NULL;
        out[count++] = parse_int(p, &endp);
        if (endp == p) break;
        p = endp;
        while (*p && *p != ':') p++;
    }
    return count;
}

void parseCommande(const char *line) {
    if (strcmp(line, "WHO") == 0) {
        PC_SERIAL.println("READY_MEGA");
        return;
    }

    if (strcmp(line, "RESET_ALL") == 0) {
        radioSendCmdToBuzzers(99);
        PC_SERIAL.println("CMD_SENT:RESET_ALL");
        envoyerSon(201, 0);
        flashDmxStartVert();
        ledPulse(150);
        for (int i = 0; i < 31; i++) dejaJoue[i] = false;
        jeuVerrouille = false;
        dernierSignal = -1;
        fenetreActive = false;
        nbBuffer = 0;
    }
    else if (strcmp(line, "RELANCE_PARTIEL") == 0) {
        radioSendCmdToBuzzers(88);
        PC_SERIAL.println("CMD_SENT:RELANCE_PARTIEL");
        envoyerSon(202, 0);
        flashDmxStartRouge();
        ledPulse(150);
        jeuVerrouille = false;
        dernierSignal = -1;
        fenetreActive = false;
        nbBuffer = 0;
    }
    else if (starts_with(line, "SET_PATCH:")) {
        int vals[5] = {0};
        uint8_t n = parse_colon_ints(line + 9, vals, 5);
        if (n >= 5) {
            // [FIX] bornage : nbCanaux entre 1 et 30, offsets entre 0 et nbCanaux-1
            int nbCanauxTmp = constrain(vals[0], 1, 30);
            settings.nbCanaux = nbCanauxTmp;
            settings.offDim   = constrain(vals[1], 0, nbCanauxTmp - 1);
            settings.offR     = constrain(vals[2], 0, nbCanauxTmp - 1);
            settings.offG     = constrain(vals[3], 0, nbCanauxTmp - 1);
            settings.offB     = constrain(vals[4], 0, nbCanauxTmp - 1);
            PC_SERIAL.println("CONF:PATCH_OK");
        } else {
            PC_SERIAL.println("ERR:PATCH_INCOMPLETE"); // [FIX] retour d'erreur explicite
        }
    }
    else if (starts_with(line, "SET_NB_EQ:")) {
        int v = parse_int(line + 10, NULL);
        settings.nbEquipes = constrain(v, 1, 30);
        PC_SERIAL.println("CONF:NB_EQUIPES_OK");
    }
    else if (starts_with(line, "SET_ADR:")) {
        int vals[2] = {0};
        uint8_t n = parse_colon_ints(line + 7, vals, 2);
        if (n >= 2) {
            int idx = vals[0];
            int adr = vals[1];
            // [FIX] validation de la plage DMX (1-512) avant écriture, avec retour d'erreur
            if (idx >= 0 && idx < 30 && adr >= 1 && adr <= 512) {
                settings.adressesDMX[idx] = adr;
                PC_SERIAL.println("CONF:ADR_OK");
            } else {
                PC_SERIAL.println("ERR:ADR_OUT_OF_RANGE");
            }
        } else {
            PC_SERIAL.println("ERR:ADR_INCOMPLETE"); // [FIX]
        }
    }
    else if (starts_with(line, "SET_COL:")) {
        int vals[5] = {0};
        uint8_t n = parse_colon_ints(line + 7, vals, 5);
        if (n >= 5) {
            int eq  = vals[0] - 1;
            int grp = vals[1];
            int r = vals[2], g = vals[3], b = vals[4];
            if (eq >= 0 && eq < 30 && grp >= 0 && grp < 30) {
                settings.couleurs[eq][grp][0] = (byte)constrain(r, 0, 255);
                settings.couleurs[eq][grp][1] = (byte)constrain(g, 0, 255);
                settings.couleurs[eq][grp][2] = (byte)constrain(b, 0, 255);
                PC_SERIAL.println("CONF:COL_OK");
            }
        }
    }
    else if (strcmp(line, "SAVE_CONFIG") == 0) {
        settings.magic = MAGIC_NUMBER;
        // [FIX] IMPORTANT : EEPROM.put() sur ~2.7 Ko peut prendre jusqu'à ~9s dans le
        // pire cas (tous les octets modifiés). Ça dépasse le délai du watchdog (4s) :
        // on le désactive temporairement pour ne pas provoquer un reset en pleine
        // écriture (ce qui corromprait l'EEPROM).
        wdt_disable();
        EEPROM.put(0, settings);
        wdt_enable(WDTO_4S);
        PC_SERIAL.println("CONF:SAVED_TO_EEPROM");
    }
    else if (strcmp(line, "RESET_V1") == 0) {
        wdt_disable(); // [FIX] même raison que SAVE_CONFIG (initialiserConfigParDefaut fait un EEPROM.put)
        initialiserConfigParDefaut();
        wdt_enable(WDTO_4S);
        PC_SERIAL.println("CONF:V1_RESTORED");
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

void clignoterVert() { flashDmxStartVert(); }
void clignoterRouge() { flashDmxStartRouge(); }

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
