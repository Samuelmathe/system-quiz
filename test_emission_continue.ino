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
uint16_t seqCounter = 0;
uint16_t nbOk = 0;
uint16_t nbTotal = 0;

void setup() {
    Serial.begin(9600);
    delay(500);

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

    Serial.println("Emission continue toutes les 500ms. Deplace le boitier pour observer la fiabilite.");
}

void loop() {
    RadioMsg msg;
    msg.kind = 1;
    msg.value = 99;
    msg.seq = ++seqCounter;

    bool ok = radio.write(&msg, sizeof(msg));
    nbTotal++;
    if (ok) nbOk++;

    Serial.print("Essai #");
    Serial.print(nbTotal);
    Serial.print(" : ");
    Serial.print(ok ? "OK" : "ECHEC");
    Serial.print("   (reussite : ");
    Serial.print(nbOk);
    Serial.print("/");
    Serial.print(nbTotal);
    Serial.print(" = ");
    Serial.print((nbOk * 100) / nbTotal);
    Serial.println("%)");

    delay(500);
}
