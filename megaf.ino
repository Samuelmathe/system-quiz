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
// [FIX] Bump 0xAD -> 0xAE : ajout de strobeDureeMs[30], la duree du
// strobe devient PAR EQUIPE (configurable ou continu) au lieu d'une
// duree fixe globale (WIN_STROBE_DURATION_MS). Sans ce bump, une EEPROM
// ecrite par l'ancienne version serait relue telle quelle.
#define MAGIC_NUMBER 0xAE
#define PC_SERIAL  Serial3   // liaison vers le PC (logiciel Node.js)
#define NANO_SERIAL Serial2  // liaison vers le RF-Nano (RX2=17, TX2=16)
#define NANO_BAUD 19200

// =========================================================
// STRUCTURE EEPROM
// =========================================================
// Un profil de canaux par projecteur -- permet de melanger des modeles
// differents (projecteur RGB simple, lyre utilisee juste pour sa couleur,
// etc.) sans jamais toucher au code : chaque projecteur a son propre
// nombre de canaux et ses propres offsets, au lieu d'un reglage unique
// impose a tous.
struct FixtureProfile {
    int nbCanaux;
    int offDim, offR, offG, offB;
    int offStrobe;      // -1 = pas de canal strobe sur ce projecteur
    byte strobeValue;   // valeur qui declenche le strobe (depend du projecteur)
};

struct QuizConfig {
    byte magic;
    int nbEquipes;
    int adressesDMX[30];
    FixtureProfile profils[30];
    byte couleurs[30][30][3];
    // Duree du strobe a l'annonce du gagnant, PAR EQUIPE (pas globale) :
    // 0 = pas de strobe pour cette equipe (affichage fixe immediat),
    // -1 = strobe continu (ne s'arrete jamais tout seul, jusqu'au
    // prochain valider/refuser), sinon duree en ms.
    int strobeDureeMs[30];
} settings;

// =========================================================
// VARIABLES D'ÉTAT
// =========================================================
bool jeuVerrouille         = false;
bool dejaJoue[31]          = {false};
int dernierSignal          = -1;
unsigned long dernierSignalTemps = 0;
unsigned long dernierAliveMs = 0;

// [FIX] Etat radio du pont RF-Nano, remonte via "STATUS:radioOK=" (voir
// parseLigneNano). Remplace l'ancien champ radioOK= du Mega d'avant le
// pont, que quiz_logger-3.py/quiz_logger.py attendaient toujours dans
// ALIVE: sans jamais le recevoir -> detection de redemarrage Mega
// silencieusement cassee depuis la migration vers le pont.
bool bridgeRadioOK = false;

// [FIX] Nombre de buzz ignores depuis le dernier ALIVE:, compte en
// silence au lieu d'un PC_SERIAL.print() par evenement (qui saturait
// Serial3 a 9600 bauds sous forte charge et pouvait, dans le pire cas,
// retarder wdt_reset() au-dela des 4s du watchdog).
uint16_t nbIgnoresDepuisAlive = 0;

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

void setProjecteur(int addr, int r, int g, int b, byte strobe, const FixtureProfile &profil);
void eteindreLumieres();
void flashDmxUpdate();
void flashDmxStartVert();
void flashDmxStartRouge();
void allumerCouleurEquipe(int equipe);
void declencherEffetGagnant(int equipe);
void updateWinEffect();
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

    // [FIX] Si une equipe a gagne PENDANT l'animation (bouton valider ->
    // clignotement vert de ~900ms), on doit arreter net sans toucher au
    // DMX : sinon la prochaine etape planifiee de l'animation ecrase la
    // couleur de l'equipe gagnante quelques centaines de ms plus tard
    // (visible en rafale : l'equipe s'allume un instant puis s'eteint).
    if (jeuVerrouille) {
        flashDmxMode = 0;
        flashDmxStep = 0;
        return;
    }

    unsigned long now = millis();
    if ((long)(now - flashDmxNextMs) < 0) return;

    bool vert = (flashDmxMode == 1);
    if (flashDmxStep % 2 == 0) {
        eteindreLumieres();
        for (int p = 0; p < 30; p++) {
            int addr = settings.adressesDMX[p];
            if (addr > 0 && addr <= 512) {
                if (vert) setProjecteur(addr, 0, 255, 0, 0, settings.profils[p]);
                else       setProjecteur(addr, 255, 0, 0, 0, settings.profils[p]);
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

    for (int p = 0; p < 30; p++) {
        settings.profils[p].nbCanaux    = 8;
        settings.profils[p].offDim      = 0;
        settings.profils[p].offR        = 1;
        settings.profils[p].offG        = 2;
        settings.profils[p].offB        = 3;
        settings.profils[p].offStrobe   = -1; // desactive tant que non configure
        settings.profils[p].strobeValue = 0;
    }

    for (int eq = 0; eq < 30; eq++) settings.strobeDureeMs[eq] = 0; // pas de strobe tant que non configure

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
        settings.nbEquipes < 1 || settings.nbEquipes > 30) {
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
    updateWinEffect();

    // BATTEMENT LED (Non-bloquant) — vivant tant que la loop tourne
    static unsigned long dernierBattement = 0;
    if (millis() - dernierBattement > 2000) {
        ledPulse(40);
        dernierBattement = millis();
    }

    // Journal de vie périodique — format complet attendu par
    // quiz_logger(-3).py (radioOK= manquait depuis le passage au pont).
    // "ignored=" porte le compte des buzz ignores depuis le dernier
    // ALIVE, a la place d'un print par evenement.
    if (millis() - dernierAliveMs > 2000) {
        dernierAliveMs = millis();
        PC_SERIAL.print("ALIVE:"); PC_SERIAL.print(millis());
        PC_SERIAL.print(",radioOK="); PC_SERIAL.print(bridgeRadioOK ? 1 : 0);
        PC_SERIAL.print(",locked="); PC_SERIAL.print(jeuVerrouille ? 1 : 0);
        PC_SERIAL.print(",ignored="); PC_SERIAL.println(nbIgnoresDepuisAlive);
        nbIgnoresDepuisAlive = 0;
    }

    // Reset anti-doublon après 1 seconde
    if (dernierSignal != -1 && millis() - dernierSignalTemps > 1000) {
        dernierSignal = -1;
    }

    // RÉCEPTION série PC
    // [FIX] wdt_reset() a chaque caractere : si un gros paquet de lignes
    // arrive d'un coup, cette boucle peut a elle seule durer plusieurs
    // secondes dans une meme iteration de loop() -- sans reset ici, ca
    // peut depasser les 4s du watchdog et provoquer un vrai reboot.
    while (PC_SERIAL.available() > 0) {
        wdt_reset();
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
    // [FIX] même raison que la boucle PC_SERIAL ci-dessus : sous rafale
    // (stress test), le pont peut envoyer beaucoup de lignes d'un coup.
    while (NANO_SERIAL.available() > 0) {
        wdt_reset();
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
            declencherEffetGagnant(signal);
            PC_SERIAL.print("BUZZ:"); PC_SERIAL.println(signal);
            // Mode sans PC : voir commentaire équivalent dans actionResetAll()
            NANO_SERIAL.print("SON:200:"); NANO_SERIAL.println(signal);
        } else {
            if (nbIgnoresDepuisAlive < 0xFFFF) nbIgnoresDepuisAlive++;
        }
    }
    else if (line[0] == 'C' && line[1] == 'M' && line[2] == 'D' && line[3] == ':') {
        // Commande venant du Nano animateur sans fil (pipe dédié, origine fiable)
        int v = (int)strtol(line + 4, NULL, 10);
        if (v == 99) actionResetAll();
        else if (v == 88) actionRelancePartiel();
    }
    else if (strncmp(line, "STATUS:radioOK=", 15) == 0) {
        // heartbeat du pont RF-Nano, relaye dans ALIVE:
        bridgeRadioOK = (line[15] == '1');
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
        // Format : SET_PATCH:<idx>:<nbCanaux>:<offDim>:<offR>:<offG>:<offB>[:<offStrobe>:<strobeValue>]
        // offStrobe/strobeValue sont optionnels : retro-compat avec un
        // logiciel de config pas encore mis a jour qui n'enverrait que les
        // 5 premiers champs (n==5) -> le canal strobe de ce projecteur
        // reste tel qu'il etait deja configure, au lieu d'etre efface.
        int vals[8] = {0};
        uint8_t n = parse_colon_ints(line + 9, vals, 8);
        if (n >= 6) {
            int idx = vals[0];
            if (idx < 0 || idx >= 30) {
                PC_SERIAL.println("ERR:PATCH_OUT_OF_RANGE");
                return;
            }
            int nbCanauxTmp = constrain(vals[1], 1, 30);
            FixtureProfile &prof = settings.profils[idx];
            prof.nbCanaux = nbCanauxTmp;
            prof.offDim   = constrain(vals[2], 0, nbCanauxTmp - 1);
            prof.offR     = constrain(vals[3], 0, nbCanauxTmp - 1);
            prof.offG     = constrain(vals[4], 0, nbCanauxTmp - 1);
            prof.offB     = constrain(vals[5], 0, nbCanauxTmp - 1);
            if (n >= 8) {
                prof.offStrobe   = constrain(vals[6], -1, nbCanauxTmp - 1);
                prof.strobeValue = (byte)constrain(vals[7], 0, 255);
            }
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
    else if (starts_with(line, "SET_STROBE_EQ:")) {
        // Format : SET_STROBE_EQ:<equipe>:<dureeMs>
        // dureeMs : 0 = pas de strobe, -1 = continu, >0 = duree en ms
        int vals[2] = {0};
        uint8_t n = parse_colon_ints(line + 13, vals, 2);
        if (n >= 2) {
            int eq = vals[0] - 1;
            if (eq >= 0 && eq < 30) {
                settings.strobeDureeMs[eq] = constrain(vals[1], -1, 32000);
                PC_SERIAL.println("CONF:STROBE_EQ_OK");
            } else {
                PC_SERIAL.println("ERR:STROBE_EQ_OUT_OF_RANGE");
            }
        } else {
            PC_SERIAL.println("ERR:STROBE_EQ_INCOMPLETE");
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
void setProjecteur(int addr, int r, int g, int b, byte strobe, const FixtureProfile &profil) {
    int maxC = constrain(profil.nbCanaux, 1, 30);
    if (profil.offDim >= 0 && profil.offDim < maxC) DMXSerial.write(addr + profil.offDim, 255);
    if (profil.offR   >= 0 && profil.offR   < maxC) DMXSerial.write(addr + profil.offR,   r);
    if (profil.offG   >= 0 && profil.offG   < maxC) DMXSerial.write(addr + profil.offG,   g);
    if (profil.offB   >= 0 && profil.offB   < maxC) DMXSerial.write(addr + profil.offB,   b);
    if (profil.offStrobe >= 0 && profil.offStrobe < maxC) DMXSerial.write(addr + profil.offStrobe, strobe);
}

void eteindreLumieres() {
    for (int p = 0; p < 30; p++) {
        int addr = settings.adressesDMX[p];
        if (addr > 0 && addr <= 512) {
            int nbCanaux = constrain(settings.profils[p].nbCanaux, 1, 30);
            for (int c = 0; c < nbCanaux; c++) {
                DMXSerial.write(addr + c, 0);
            }
        }
    }
}

// Affichage FIXE (sans strobe) de la couleur d'une equipe — etat final,
// utilise directement (validation manuelle) ou via updateWinEffect() une
// fois le strobe d'annonce termine.
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
                settings.couleurs[idxEq][p][2],
                0,
                settings.profils[p]
            );
        }
    }
}

// =========================================================
// EFFET D'ANNONCE DU GAGNANT : strobe sur la couleur de l'equipe, puis
// affichage fixe -- reglage PAR EQUIPE (settings.strobeDureeMs), pas une
// duree unique pour tout le monde :
//   0  -> pas de strobe du tout, affichage fixe immediat
//   -1 -> strobe continu, ne bascule jamais tout seul (jusqu'au prochain
//         valider/refuser)
//   >0 -> strobe pendant ce nombre de ms puis bascule en fixe
// Non-bloquant (comme flashDmxUpdate) : declencherEffetGagnant() ecrit
// l'etat initial tout de suite, updateWinEffect() gere la suite.
// =========================================================
bool winEffectActive = false;
int winEffectSignal = -1;
int winEffectDureeMs = 0;
unsigned long winEffectStartMs = 0;

void declencherEffetGagnant(int equipe) {
    int idxEq = equipe - 1;
    if (idxEq < 0 || idxEq >= 30) return;

    int duree = settings.strobeDureeMs[idxEq];
    if (duree == 0) {
        // Cette equipe n'a pas de strobe configure -> affichage direct.
        allumerCouleurEquipe(equipe);
        return;
    }

    eteindreLumieres();
    for (int p = 0; p < 30; p++) {
        int addr = settings.adressesDMX[p];
        if (addr > 0 && addr <= 512) {
            setProjecteur(addr,
                settings.couleurs[idxEq][p][0],
                settings.couleurs[idxEq][p][1],
                settings.couleurs[idxEq][p][2],
                settings.profils[p].strobeValue,
                settings.profils[p]
            );
        }
    }
    winEffectActive = true;
    winEffectSignal = equipe;
    winEffectDureeMs = duree;
    winEffectStartMs = millis();
}

void updateWinEffect() {
    if (!winEffectActive) return;

    // [FIX] meme logique defensive que flashDmxUpdate() : si le jeu a ete
    // deverrouille entre-temps (reset/relance), on annule sans toucher au
    // DMX plutot que d'ecraser l'animation de reset avec un affichage fixe
    // perime.
    if (!jeuVerrouille) {
        winEffectActive = false;
        return;
    }

    if (winEffectDureeMs < 0) return; // strobe continu : ne bascule jamais tout seul

    if ((long)(millis() - winEffectStartMs) < winEffectDureeMs) return;

    winEffectActive = false;
    allumerCouleurEquipe(winEffectSignal); // bascule strobe -> fixe
}
