#include <DMXSerial.h>
#include <EEPROM.h>
#include <string.h>
#include <stdlib.h>
#include <avr/wdt.h>

// =========================================================
// SÉCURITÉ WATCHDOG : désactivation ultra-précoce
// =========================================================
uint8_t mcusrSauvegarde __attribute__ ((section (".noinit")));
void getMcusrEtStopperWdt(void) __attribute__((naked)) __attribute__((section(".init3")));
void getMcusrEtStopperWdt(void) {
    mcusrSauvegarde = MCUSR;
    MCUSR = 0;
    wdt_disable();
}

// =========================================================
// CONFIGURATION MATÉRIELLE
// =========================================================
#define LED 13
#define MAGIC_NUMBER 0xAB
#define PC_SERIAL  Serial3   // liaison vers le PC (logiciel Node.js)
#define NANO_SERIAL Serial2  // liaison vers le RF-Nano (RX2=17, TX2=16)
#define NANO_BAUD 19200

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
int dernierSignal          = -1;
unsigned long dernierSignalTemps = 0;
unsigned long dernierAliveMs = 0;

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

// ---- Buffers lignes série (PC et Nano séparés) ----
#define SERIAL_BUF_LEN 96
char serialBuf[SERIAL_BUF_LEN];
uint8_t serialLen = 0;

char nanoBuf[SERIAL_BUF_LEN];
uint8_t nanoLen = 0;

void setProjecteur(int addr, int r, int g, int b);
void eteindreLumieres();
void flashDmxUpdate();
void flashDmxStartVert();
void flashDmxStartRouge();
void allumerCouleurEquipe(int equipe);
void parseCommande(const char *line);
void parseLigneNano(const char *line);

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
    EEPROM.put(0, settings);
}

// =========================================================
// RELAIS VERS LE NANO (remplace radioSendCmdToBuzzers / envoyerSon)
// =========================================================
void envoyerCmdAuxEquipes(uint8_t value) {
    NANO_SERIAL.print("CMD:");
    NANO_SERIAL.println(value);
}

// =========================================================
// SETUP
// =========================================================
void setup() {
    pinMode(LED, OUTPUT);
    DMXSerial.init(DMXController);
    eteindreLumieres();

    EEPROM.get(0, settings);
    if (settings.magic != MAGIC_NUMBER ||
        settings.nbEquipes < 1 || settings.nbEquipes > 30 ||
        settings.nbCanaux  < 1 || settings.nbCanaux  > 30) {
        initialiserConfigParDefaut();
    }

    PC_SERIAL.begin(9600);
    NANO_SERIAL.begin(NANO_BAUD);

    for (int i = 0; i < 3; i++) {
        digitalWrite(LED, HIGH);
        unsigned long t0 = millis();
        while (millis() - t0 < 60) {}
        digitalWrite(LED, LOW);
        t0 = millis();
        while (millis() - t0 < 60) {}
    }

    eteindreLumieres();
    wdt_enable(WDTO_4S);
}

// =========================================================
// LOOP PRINCIPALE
// =========================================================
void loop() {
    wdt_reset();

    ledUpdate();
    flashDmxUpdate();

    // BATTEMENT LED (Non-bloquant) — vivant tant que la loop tourne
    static unsigned long dernierBattement = 0;
    if (millis() - dernierBattement > 2000) {
        ledPulse(40);
        dernierBattement = millis();
    }

    // Journal de vie périodique
    if (millis() - dernierAliveMs > 2000) {
        dernierAliveMs = millis();
        PC_SERIAL.print("ALIVE:"); PC_SERIAL.print(millis());
        PC_SERIAL.print(",locked="); PC_SERIAL.println(jeuVerrouille ? 1 : 0);
    }

    // Reset anti-doublon après 1 seconde
    if (dernierSignal != -1 && millis() - dernierSignalTemps > 1000) {
        dernierSignal = -1;
    }

    // RÉCEPTION série PC
    while (PC_SERIAL.available() > 0) {
        char c = (char)PC_SERIAL.read();
        if (c == '\r') continue;
        if (c == '\n') {
            serialBuf[serialLen] = '\0';
            while (serialLen > 0 && (serialBuf[serialLen - 1] == ' ' || serialBuf[serialLen - 1] == '\t')) {
                serialBuf[--serialLen] = '\0';
            }
            uint8_t start = 0;
            while (serialBuf[start] == ' ' || serialBuf[start] == '\t') start++;
            if (serialLen > start) parseCommande(serialBuf + start);
            serialLen = 0;
        } else if (serialLen < SERIAL_BUF_LEN - 1) {
            serialBuf[serialLen++] = c;
        } else {
            serialLen = 0;
        }
    }

    // RÉCEPTION série NANO (buzz déjà résolus, un seul gagnant par message)
    while (NANO_SERIAL.available() > 0) {
        char c = (char)NANO_SERIAL.read();
        if (c == '\r') continue;
        if (c == '\n') {
            nanoBuf[nanoLen] = '\0';
            if (nanoLen > 0) parseLigneNano(nanoBuf);
            nanoLen = 0;
        } else if (nanoLen < SERIAL_BUF_LEN - 1) {
            nanoBuf[nanoLen++] = c;
        } else {
            nanoLen = 0;
        }
    }
}

// =========================================================
// ACTIONS DE JEU (déclenchables par le PC OU le Nano animateur)
// =========================================================
void actionResetAll() {
    envoyerCmdAuxEquipes(99);
    PC_SERIAL.println("CMD_SENT:RESET_ALL");
    // Mode sans PC : ordre son relayé au RF-Nano, qui le retransmet en radio
    // au nano son (pipe "00002"). Si un PC est branché, ce message est
    // simplement ignoré côté RF-Nano tant qu'aucun nano son n'est présent.
    NANO_SERIAL.println("SON:201:0");
    flashDmxStartVert();
    ledPulse(150);
    for (int i = 0; i < 31; i++) dejaJoue[i] = false;
    jeuVerrouille = false;
    dernierSignal = -1;
}

void actionRelancePartiel() {
    envoyerCmdAuxEquipes(88);
    PC_SERIAL.println("CMD_SENT:RELANCE_PARTIEL");
    // Mode sans PC : voir commentaire équivalent dans actionResetAll()
    NANO_SERIAL.println("SON:202:0");
    flashDmxStartRouge();
    ledPulse(150);
    jeuVerrouille = false;
    dernierSignal = -1;
}

// =========================================================
// TRAITEMENT DES LIGNES REÇUES DU NANO (buzz résolus + commandes
// venant du Nano animateur sans fil, déjà validées par son pipe
// radio dédié côté RF-Nano)
// =========================================================
void parseLigneNano(const char *line) {
    if (line[0] == 'B' && line[1] == 'U' && line[2] == 'Z' && line[3] == 'Z' && line[4] == ':') {
        int signal = (int)strtol(line + 5, NULL, 10);

        if (signal == dernierSignal) return; // anti-doublon simple sur répétition immédiate
        dernierSignal = signal;
        dernierSignalTemps = millis();
        ledPulse(60);

        if (signal >= 1 && signal <= settings.nbEquipes && !dejaJoue[signal] && !jeuVerrouille) {
            jeuVerrouille = true;
            dejaJoue[signal] = true;
            allumerCouleurEquipe(signal);
            PC_SERIAL.print("BUZZ:"); PC_SERIAL.println(signal);
            // Mode sans PC : voir commentaire équivalent dans actionResetAll()
            NANO_SERIAL.print("SON:200:"); NANO_SERIAL.println(signal);
        } else {
            PC_SERIAL.print("IGNORED:locked="); PC_SERIAL.print(jeuVerrouille ? 1 : 0);
            PC_SERIAL.print(",team="); PC_SERIAL.println(signal);
        }
    }
    else if (line[0] == 'C' && line[1] == 'M' && line[2] == 'D' && line[3] == ':') {
        // Commande venant du Nano animateur sans fil (pipe dédié, origine fiable)
        int v = (int)strtol(line + 4, NULL, 10);
        if (v == 99) actionResetAll();
        else if (v == 88) actionRelancePartiel();
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
    while (*prefix) { if (*s++ != *prefix++) return false; }
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
        actionResetAll();
    }
    else if (strcmp(line, "RELANCE_PARTIEL") == 0) {
        actionRelancePartiel();
    }
    else if (starts_with(line, "SET_PATCH:")) {
        int vals[5] = {0};
        uint8_t n = parse_colon_ints(line + 9, vals, 5);
        if (n >= 5) {
            int nbCanauxTmp = constrain(vals[0], 1, 30);
            settings.nbCanaux = nbCanauxTmp;
            settings.offDim   = constrain(vals[1], 0, nbCanauxTmp - 1);
            settings.offR     = constrain(vals[2], 0, nbCanauxTmp - 1);
            settings.offG     = constrain(vals[3], 0, nbCanauxTmp - 1);
            settings.offB     = constrain(vals[4], 0, nbCanauxTmp - 1);
            PC_SERIAL.println("CONF:PATCH_OK");
        } else {
            PC_SERIAL.println("ERR:PATCH_INCOMPLETE");
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
            if (idx >= 0 && idx < 30 && adr >= 1 && adr <= 512) {
                settings.adressesDMX[idx] = adr;
                PC_SERIAL.println("CONF:ADR_OK");
            } else {
                PC_SERIAL.println("ERR:ADR_OUT_OF_RANGE");
            }
        } else {
            PC_SERIAL.println("ERR:ADR_INCOMPLETE");
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
            } else {
                PC_SERIAL.println("ERR:COL_OUT_OF_RANGE");
            }
        } else {
            PC_SERIAL.println("ERR:COL_INCOMPLETE");
        }
    }
    else if (strcmp(line, "SAVE_CONFIG") == 0) {
        settings.magic = MAGIC_NUMBER;
        wdt_disable();
        EEPROM.put(0, settings);
        wdt_enable(WDTO_4S);
        PC_SERIAL.println("CONF:SAVED_TO_EEPROM");
    }
    else if (strcmp(line, "RESET_V1") == 0) {
        wdt_disable();
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
