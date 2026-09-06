#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(9, 10);
const byte adresse[6] = "00001";

#define RADIO_CHANNEL 108

struct RadioMsg {
    uint8_t kind;
    uint8_t value;
    uint16_t seq;
};

uint16_t nbRecus = 0;

void setup() {
    Serial.begin(9600);
    delay(300);

    if (!radio.begin()) {
        Serial.println(">>> ECHEC : radio.begin() a echoue.");
        while (1) {}
    }
    radio.setChannel(RADIO_CHANNEL);
    radio.setAddressWidth(5);
    radio.openReadingPipe(1, adresse);
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setPayloadSize(sizeof(RadioMsg));
    radio.setAutoAck(true);
    radio.setRetries(10, 15);
    radio.startListening();

    Serial.println("RECEPTEUR pret. En attente de messages...");
}

void loop() {
    if (radio.available()) {
        RadioMsg msg;
        radio.read(&msg, sizeof(msg));
        nbRecus++;

        Serial.print(">>> Recu #");
        Serial.print(nbRecus);
        Serial.print(" : kind=");
        Serial.print(msg.kind);
        Serial.print(" value=");
        Serial.print(msg.value);
        Serial.print(" seq=");
        Serial.println(msg.seq);
    }
}
