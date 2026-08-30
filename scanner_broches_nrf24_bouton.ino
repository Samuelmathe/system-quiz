#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

const uint8_t brochesLibres[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, A0, A1, A2, A3, A4, A5};
const uint8_t nbBroches = sizeof(brochesLibres) / sizeof(brochesLibres[0]);
bool dernierEtat[15];
uint8_t brochesBouton[15];
uint8_t nbBrochesBouton = 0;
uint8_t cePin = 255, csnPin = 255;

void nomBroche(uint8_t pin, char *buf) {
    if (pin >= A0) sprintf(buf, "A%d", pin - A0);
    else sprintf(buf, "D%d", pin);
}

void scannerRadio() {
    Serial.println("Recherche du module nRF24 (CE/CSN)...");
    for (uint8_t i = 0; i < nbBroches; i++) {
        for (uint8_t j = 0; j < nbBroches; j++) {
            if (i == j) continue;
            uint8_t ce = brochesLibres[i];
            uint8_t csn = brochesLibres[j];
            RF24 radio(ce, csn);
            if (radio.begin() && radio.isChipConnected()) {
                cePin = ce;
                csnPin = csn;
                char bufCe[6], bufCsn[6];
                nomBroche(ce, bufCe);
                nomBroche(csn, bufCsn);
                Serial.print(">>> nRF24 detecte : CE=");
                Serial.print(bufCe);
                Serial.print(" CSN=");
                Serial.println(bufCsn);
                return;
            }
        }
    }
    Serial.println(">>> Aucun nRF24 detecte.");
}

bool appuiStable(uint8_t pin) {
    for (uint8_t k = 0; k < 4; k++) {
        if (digitalRead(pin) != LOW) return false;
        delay(3);
    }
    return true;
}

void scannerBouton() {
    Serial.println("Appuie sur le bouton poussoir...");
    unsigned long fin = millis() + 15000;
    while (millis() < fin) {
        for (uint8_t i = 0; i < nbBrochesBouton; i++) {
            uint8_t pin = brochesBouton[i];
            bool etat = digitalRead(pin);
            if (etat == LOW && dernierEtat[i] == HIGH && appuiStable(pin)) {
                char buf[6];
                nomBroche(pin, buf);
                Serial.print(">>> Bouton detecte sur : ");
                Serial.println(buf);
            }
            dernierEtat[i] = digitalRead(pin);
        }
        delay(20);
    }
}

void setup() {
    Serial.begin(9600);
    delay(500);
    scannerRadio();

    for (uint8_t i = 0; i < nbBroches; i++) {
        uint8_t pin = brochesLibres[i];
        if (pin == cePin || pin == csnPin) continue;
        pinMode(pin, INPUT_PULLUP);
        brochesBouton[nbBrochesBouton] = pin;
        dernierEtat[nbBrochesBouton] = HIGH;
        nbBrochesBouton++;
    }
    scannerBouton();
    Serial.println("Termine.");
}

void loop() {}
