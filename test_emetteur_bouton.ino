#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(9, 10);
const byte adresse[6] = "00001";
const int boutonPin = 2;

#define RADIO_CHANNEL 108

struct RadioMsg {
    uint8_t kind;
    uint8_t value;
    uint16_t seq;
};
uint16_t seqCounter = 0;

bool lastBtn = true;
unsigned long lastChangeMs = 0;
const unsigned long DEBOUNCE_MS = 25;

void setup() {
    Serial.begin(9600);
    pinMode(boutonPin, INPUT_PULLUP);
    delay(300);

    if (!radio.begin()) {
        Serial.println(">>> ECHEC : radio.begin() a echoue.");
        while (1) {}
    }
    radio.setChannel(RADIO_CHANNEL);
    radio.setAddressWidth(5);
    radio.openWritingPipe(adresse);
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setPayloadSize(sizeof(RadioMsg));
    radio.setAutoAck(true);
    radio.setRetries(10, 15);
    radio.stopListening();

    Serial.println("EMETTEUR pret. Appuie sur le bouton (D2).");
}

void loop() {
    bool btn = (digitalRead(boutonPin) == LOW);
    unsigned long now = millis();

    static bool prevBtn = false;
    if (btn != prevBtn) {
        prevBtn = btn;
        lastChangeMs = now;
    }

    static bool dejaEnvoye = false;
    if (btn && (now - lastChangeMs) >= DEBOUNCE_MS) {
        if (!dejaEnvoye) {
            dejaEnvoye = true;

            RadioMsg msg;
            msg.kind = 1;
            msg.value = 1;
            msg.seq = ++seqCounter;

            bool ok = radio.write(&msg, sizeof(msg));

            Serial.print("Envoi #");
            Serial.print(msg.seq);
            Serial.println(ok ? " -> OK (accuse de reception recu)" : " -> ECHEC (Ack non recu)");
        }
    } else {
        dejaEnvoye = false;
    }
}
