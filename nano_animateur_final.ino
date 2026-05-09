#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(9, 10);
const byte adresse[6] = "00001";
const int boutonResetAll = 2;
const int boutonRelance  = 3;

void setup() {
    Serial.begin(9600);
    pinMode(boutonResetAll, INPUT_PULLUP);
    pinMode(boutonRelance,  INPUT_PULLUP);
    pinMode(LED_BUILTIN,    OUTPUT);

    if (radio.begin()) {
        radio.setChannel(108);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setAutoAck(false);
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

// Envoi 5x pour fiabilite maximale a distance
void envoyerSignal(int signal) {
    for (int i = 0; i < 5; i++) {
        radio.write(&signal, sizeof(signal));
        delay(5);
    }
}

void loop() {
    // BOUTON RESET_ALL (Bonne reponse)
    if (digitalRead(boutonResetAll) == LOW) {
        delay(20);
        if (digitalRead(boutonResetAll) != LOW) return;
        digitalWrite(LED_BUILTIN, HIGH);
        envoyerSignal(99); // 5x pour fiabilite
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
        envoyerSignal(88); // 5x pour fiabilite
        Serial.println("ENVOYE : 88 (RELANCE_PARTIEL)");
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        delay(300);
        while (digitalRead(boutonRelance) == LOW);
    }
}
