#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <DMXSerial.h>
#include <EEPROM.h>
#include <avr/wdt.h>

// =========================================================
// CONFIGURATION MATÉRIELLE
// =========================================================
RF24 radio(9, 53);

const byte adresseBuzzers[6] = "00001";
const byte adresseSon[6]     = "00002";

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
    radio.write(&signal, sizeof(signal));
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
        radio.setChannel(108);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setAutoAck(false);
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

    // BATTEMENT LED toutes les 2 secondes
    if (radioOK && millis() - dernierBattement > 2000) {
        digitalWrite(LED, HIGH); delay(50);
        digitalWrite(LED, LOW);
        dernierBattement = millis();
    }

    // Reset anti-doublon après 1 seconde
    if (dernierSignal != -1 && millis() - dernierSignalTemps > 1000) {
        dernierSignal = -1;
    }

    // RÉCEPTION SERIAL3 (Logiciels PC)
    if (Serial3.available() > 0) {
        String line = Serial3.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) parseCommande(line);
    }

    // RÉCEPTION RADIO buzzers
    if (radio.available()) {
        int signal = 0;
        radio.read(&signal, sizeof(signal));

        // Anti-doublon
        if (signal == dernierSignal) return;
        dernierSignal      = signal;
        dernierSignalTemps = millis();

        // Clignotement = signal reçu
        for (int i = 0; i < 5; i++) {
            digitalWrite(LED, HIGH); delay(30);
            digitalWrite(LED, LOW);  delay(30);
        }

        // C'est une équipe → buffer fenêtre simultanée
        if (signal >= 1 && signal <= settings.nbEquipes) {
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
        else if (signal == 99) {
            // NOTIFIER LE LOGICIEL EN PREMIER → zéro latence !
            Serial3.println("CMD_SENT:RESET_ALL");
            envoyerSon(201);

            for (int i = 0; i < 31; i++) dejaJoue[i] = false;
            jeuVerrouille = false;
            dernierSignal = -1;

            // Clignotement APRÈS notification → pas de blocage
            clignoterVert();
            eteindreLumieres();

            int poubelle;
            while (radio.available()) { radio.read(&poubelle, sizeof(poubelle)); }
        }

        // 88 = RELANCE_PARTIEL (Nano animateur mauvaise réponse)
        else if (signal == 88) {
            // NOTIFIER LE LOGICIEL EN PREMIER → zéro latence !
            Serial3.println("CMD_SENT:RELANCE_PARTIEL");
            envoyerSon(202);

            jeuVerrouille = false;
            dernierSignal = -1;

            // Clignotement APRÈS notification → pas de blocage
            clignoterRouge();
            eteindreLumieres();

            int poubelle;
            while (radio.available()) { radio.read(&poubelle, sizeof(poubelle)); }
        }
    }
}

// =========================================================
// PARSING COMMANDES (Logiciels PC via Serial3)
// =========================================================
void parseCommande(String line) {

    // ---- LOGICIEL SCORES ----

    // Bonne réponse via PC (VALIDER)
    if (line == "RESET_ALL") {
        radio.stopListening();
        radio.openWritingPipe(adresseBuzzers);
        int sig = 99;
        radio.write(&sig, sizeof(sig));
        radio.startListening();
        Serial3.println("CMD_SENT:RESET_ALL");

        envoyerSon(201);
        clignoterVert();
        eteindreLumieres();

        for (int i = 0; i < 31; i++) dejaJoue[i] = false;
        jeuVerrouille = false;
        dernierSignal = -1;
        fenetreActive = false;
        nbBuffer = 0;
    }

    // Mauvaise réponse via PC (REFUSER)
    else if (line == "RELANCE_PARTIEL") {
        radio.stopListening();
        radio.openWritingPipe(adresseBuzzers);
        int sig = 88;
        radio.write(&sig, sizeof(sig));
        radio.startListening();
        Serial3.println("CMD_SENT:RELANCE_PARTIEL");

        envoyerSon(202);
        clignoterRouge();
        eteindreLumieres();

        jeuVerrouille = false;
        dernierSignal = -1;
        fenetreActive = false;
        nbBuffer = 0;
    }

    // ---- LOGICIEL CONFIG ----

    else if (line.startsWith("SET_PATCH:")) {
        int p1 = line.indexOf(':', 10);
        int p2 = line.indexOf(':', p1 + 1);
        int p3 = line.indexOf(':', p2 + 1);
        int p4 = line.indexOf(':', p3 + 1);
        settings.nbCanaux = line.substring(10, p1).toInt();
        settings.offDim   = line.substring(p1 + 1, p2).toInt();
        settings.offR     = line.substring(p2 + 1, p3).toInt();
        settings.offG     = line.substring(p3 + 1, p4).toInt();
        settings.offB     = line.substring(p4 + 1).toInt();
        Serial3.println("CONF:PATCH_OK");
    }

    else if (line.startsWith("SET_NB_EQ:")) {
        settings.nbEquipes = constrain(line.substring(10).toInt(), 1, 30);
        Serial3.println("CONF:NB_EQUIPES_OK");
    }

    else if (line.startsWith("SET_ADR:")) {
        int split = line.indexOf(':', 8);
        int idx   = line.substring(8, split).toInt();
        int adr   = line.substring(split + 1).toInt();
        if (idx >= 0 && idx < 30) settings.adressesDMX[idx] = adr;
        Serial3.println("CONF:ADR_OK");
    }

    else if (line.startsWith("SET_COL:")) {
        int p1 = line.indexOf(':', 8);
        int p2 = line.indexOf(':', p1 + 1);
        int p3 = line.indexOf(':', p2 + 1);
        int p4 = line.indexOf(':', p3 + 1);
        int eq  = line.substring(8, p1).toInt() - 1;
        int grp = line.substring(p1 + 1, p2).toInt();
        if (eq >= 0 && eq < 30 && grp >= 0 && grp < 30) {
            settings.couleurs[eq][grp][0] = (byte)constrain(line.substring(p2+1, p3).toInt(), 0, 255);
            settings.couleurs[eq][grp][1] = (byte)constrain(line.substring(p3+1, p4).toInt(), 0, 255);
            settings.couleurs[eq][grp][2] = (byte)constrain(line.substring(p4+1).toInt(),     0, 255);
            Serial3.println("CONF:COL_OK");
        }
    }

    else if (line == "SAVE_CONFIG") {
        settings.magic = MAGIC_NUMBER;
        EEPROM.put(0, settings);
        Serial3.println("CONF:SAVED_TO_EEPROM");
    }

    else if (line == "RESET_V1") {
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
