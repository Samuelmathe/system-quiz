#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <avr/wdt.h>

RF24 radio(9, 10);
const byte adresse[6] = "00001";
const int boutonResetAll = 2;
const int boutonRelance  = 3;

#define RADIO_CHANNEL  108

// Message radio (doit matcher la MEGA)
struct RadioMsg {
    uint8_t kind;   // 2=CMD
    uint8_t value;  // 88/99
    uint16_t seq;   // increment
};
uint16_t seqCounter = 0;

void setup() {
    Serial.begin(9600);
    pinMode(boutonResetAll, INPUT_PULLUP);
    pinMode(boutonRelance,  INPUT_PULLUP);
    pinMode(LED_BUILTIN,    OUTPUT);
    wdt_enable(WDTO_2S);

    if (radio.begin()) {
        radio.setChannel(RADIO_CHANNEL);
        radio.setAddressWidth(5);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setCRCLength(RF24_CRC_16);
        radio.setPayloadSize(sizeof(RadioMsg));
        // Fiabilité: AutoAck + retries
        radio.setAutoAck(true);
        radio.setRetries(10, 15);
        radio.openWritingPipe(adresse);
        radio.stopListening();
        for (int i = 0; i < 3; i++) {
            digitalWrite(LED_BUILTIN, HIGH); delay(100);
            digitalWrite(LED_BUILTIN, LOW);  delay(100);
        }
        Serial.println("NANO ANIMATEUR V2 PRET !");
    } else {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(2000);
        digitalWrite(LED_BUILTIN, LOW);
    }
}

// Envoi fiable: AutoAck + quelques tentatives
void envoyerSignal(uint8_t signal) {
    RadioMsg msg;
    msg.kind = 2;
    msg.value = signal;
    msg.seq = ++seqCounter;

    bool ok = false;
    for (int i = 0; i < 5 && !ok; i++) {
        ok = radio.write(&msg, sizeof(msg));
        if (!ok) delay(10);
    }
}

void loop() {
    wdt_reset();
    // BOUTON RESET_ALL (Bonne reponse)
    if (digitalRead(boutonResetAll) == LOW) {
        delay(20);
        if (digitalRead(boutonResetAll) != LOW) return;
        digitalWrite(LED_BUILTIN, HIGH);
        envoyerSignal(99);
        Serial.println("ENVOYE : 99 (RESET_ALL)");
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        delay(300);
        while (digitalRead(boutonResetAll) == LOW);
    }

    // BOUTON RELANCE_PARTIEL (Mauvaise reponse)
    if (digitalRead(boutonRelance) == LOW) {
        delay(20);
        if (digitalRead(boutonRelance) != LOW) return;
        digitalWrite(LED_BUILTIN, HIGH);
        envoyerSignal(88);
        Serial.println("ENVOYE : 88 (RELANCE_PARTIEL)");
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        delay(300);
        while (digitalRead(boutonRelance) == LOW);
    }
}
