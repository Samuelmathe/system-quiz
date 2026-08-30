#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

RF24 radio(9, 10);

void setup() {
    Serial.begin(9600);
    delay(500);
    Serial.println("Test nRF24 -- branchement standard (CE=D9, CSN=D10)");

    bool ok = radio.begin();
    if (!ok) {
        Serial.println(">>> ECHEC : radio.begin() a echoue. Verifie CE(D9), CSN(D10), MOSI(D11), MISO(D12), SCK(D13), VCC sur 3.3V, GND.");
        return;
    }

    if (radio.isChipConnected()) {
        Serial.println(">>> OK : puce nRF24 detectee et repond correctement sur D9/D10.");
    } else {
        Serial.println(">>> ECHEC : radio.begin() a reussi mais la puce ne repond pas (isChipConnected = false).");
    }
}

void loop() {}
