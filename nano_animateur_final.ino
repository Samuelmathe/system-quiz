// =========================================================
// NANO ANIMATEUR — TÉLÉCOMMANDE SANS FIL
// =========================================================
// 2 boutons :
//   D2 = VALIDER  (bonne réponse) -> envoie CMD 99 (RESET_ALL)
//   D3 = REFUSER  (mauvaise réponse) -> envoie CMD 88 (RELANCE_PARTIEL)
//
// Émet sur le pipe dédié "adresseAnimateur" (00003), différent de
// celui des équipes (00001) — le RF-Nano bridge sait donc que tout
// message reçu sur ce pipe vient forcément de cette télécommande,
// sans avoir besoin de vérifier autre chose.
//
// Câblage boutons : D2/D3 -> bouton -> GND (utilise les pull-up
// internes, donc pas de résistance externe nécessaire).
//
// Pins nRF24 (standard RF-Nano / shield Nano) : CE=9, CSN=10,
// MOSI=11, MISO=12, SCK=13.
// =========================================================

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(9, 10); // CE, CSN

// Émet sur la MÊME adresse que les équipes ("00001"), distingué uniquement
// par le champ "kind" du message (kind=2). Le pipe 2 séparé a été abandonné :
// test terrain a montré qu'il ne s'active pas de façon fiable sur ce clone
// nRF24, alors que le pipe 1 (équipes) fonctionne correctement.
const byte adresseBuzzers[6] = "00001";

#define RADIO_CHANNEL 108
#define PIN_VALIDER 2
#define PIN_REFUSER 3
#define LED 13

#define DEBOUNCE_MS 250

struct RadioMsg {
    uint8_t kind;   // 2 = CMD
    uint8_t value;  // 99 = RESET_ALL, 88 = RELANCE_PARTIEL
    uint16_t seq;
};

static uint16_t seqCounter = 0;
unsigned long dernierAppuiValider = 0;
unsigned long dernierAppuiRefuser = 0;

// ---- Feedback LED par NOMBRE de clignotements (plus lisible qu'une durée) ----
void blinkPattern(uint8_t nBlinks, unsigned long onMs, unsigned long offMs) {
    for (uint8_t i = 0; i < nBlinks; i++) {
        digitalWrite(LED, HIGH);
        unsigned long t0 = millis();
        while (millis() - t0 < onMs) {}
        digitalWrite(LED, LOW);
        t0 = millis();
        while (millis() - t0 < offMs) {}
    }
}

void envoyerCommande(uint8_t value) {
    RadioMsg msg = {2, value, ++seqCounter};
    radio.stopListening();
    radio.openWritingPipe(adresseBuzzers);
    bool ok = false;
    for (uint8_t retry = 0; retry < 5 && !ok; retry++) {
        ok = radio.write(&msg, sizeof(msg));
        if (!ok) delayMicroseconds(500);
    }
    radio.startListening();
    // 1 clignotement = envoyé avec succès, 3 clignotements rapides = échec
    if (ok) blinkPattern(1, 150, 0);
    else    blinkPattern(3, 100, 100);
}

void setup() {
    pinMode(LED, OUTPUT);
    pinMode(PIN_VALIDER, INPUT_PULLUP);
    pinMode(PIN_REFUSER, INPUT_PULLUP);

    radio.begin();
    radio.setChannel(RADIO_CHANNEL);
    radio.setAddressWidth(5);
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setPayloadSize(sizeof(RadioMsg));
    radio.setAutoAck(true);
    radio.setRetries(10, 15);
    radio.openWritingPipe(adresseBuzzers);
    radio.stopListening(); // ce device n'écoute jamais, il n'envoie que

    // Confirmation de démarrage
    for (int i = 0; i < 2; i++) {
        digitalWrite(LED, HIGH);
        unsigned long t0 = millis();
        while (millis() - t0 < 80) {}
        digitalWrite(LED, LOW);
        t0 = millis();
        while (millis() - t0 < 80) {}
    }
}

void loop() {

    if (digitalRead(PIN_VALIDER) == LOW && millis() - dernierAppuiValider > DEBOUNCE_MS) {
        dernierAppuiValider = millis();
        envoyerCommande(99); // VALIDER -> RESET_ALL
    }

    if (digitalRead(PIN_REFUSER) == LOW && millis() - dernierAppuiRefuser > DEBOUNCE_MS) {
        dernierAppuiRefuser = millis();
        envoyerCommande(88); // REFUSER -> RELANCE_PARTIEL
    }
}
