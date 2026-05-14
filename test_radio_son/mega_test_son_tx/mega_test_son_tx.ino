
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(9, 53);

const byte adresseSon[6] = "00002";
#define RADIO_CHANNEL 108
#define LED_PIN 13

struct SonPayload {
    uint16_t cmd;
    uint8_t team;
    uint8_t reserved;
};

void envoyerSonPayload(uint16_t cmd, uint8_t team) {
    SonPayload p = {cmd, team, 0};
    radio.stopListening();
    delayMicroseconds(200);
    radio.openWritingPipe(adresseSon);
    /* Nano en setAutoAck(false) : il ne renvoie pas d'ACK. Il faut write(..., true) = paquet NO_ACK
       (W_TX_PAYLOAD_NO_ACK). Sinon le PTX attend un ACK, MAX_RT -> write() = faux = "TX FAIL". */
    radio.setAutoAck(false);
    bool ok = radio.write(&p, sizeof(p), true);
    bool ok2 = true;
    if (cmd != 200) {
        delay(8);
        ok2 = radio.write(&p, sizeof(p), true);
    }
    radio.setAutoAck(true);
    /* Ne pas startListening() : cette Mega ne recoit pas le nano son. Evite un etat RX bizarre. */
    Serial.print((ok && ok2) ? F("TX OK  ") : F("TX FAIL "));
    Serial.print(F("cmd="));
    Serial.print(cmd);
    Serial.print(F(" team="));
    Serial.println(team);
}

void setup() {
    Serial.begin(9600);
    pinMode(LED_PIN, OUTPUT);

    if (!radio.begin()) {
        Serial.println(F("ERREUR: module nRF24L01+ non detecte."));
        while (true) {
            digitalWrite(LED_PIN, HIGH);
            delay(200);
            digitalWrite(LED_PIN, LOW);
            delay(200);
        }
    }

    radio.setChannel(RADIO_CHANNEL);
    radio.setAddressWidth(5);
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setPayloadSize(sizeof(SonPayload));
    radio.setRetries(5, 15);
    radio.setAutoAck(false);
    radio.openWritingPipe(adresseSon);
    radio.stopListening();

    Serial.println(F("=== Mega TEST envoi son (adresse 00002, canal 108) ==="));
    Serial.println(F("Cycle ~11,5 s : 200, pause ~2,8 s, 201, pause ~2,8 s, 202 (marge Nano)."));
}

unsigned long lastSend = 0;

void loop() {
    unsigned long now = millis();
    if (now - lastSend < 11500UL) {
        return;
    }
    lastSend = now;

    digitalWrite(LED_PIN, HIGH);

    Serial.println(F("--- 200 BUZZ equipe 1 ---"));
    envoyerSonPayload(200, 1);
    delay(2800);

    Serial.println(F("--- 201 VICTOIRE ---"));
    envoyerSonPayload(201, 0);
    delay(2800);

    Serial.println(F("--- 202 ECHEC ---"));
    envoyerSonPayload(202, 0);

    digitalWrite(LED_PIN, LOW);
}
