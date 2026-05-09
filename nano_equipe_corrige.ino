#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// =========================================================
// CONFIGURATION
// =========================================================
RF24 radio(9, 10); // CE sur 9, CSN sur 10
const byte adresse[6] = "00001";
const int boutonPin  = 2;
const int moteurPin  = 3; // Vibreur

// --- ID DE L'EQUIPE (modifier de 1 a 8) ---
int monEquipe = 3;

// =========================================================
// SETUP
// =========================================================
void setup() {
    Serial.begin(9600);
    pinMode(boutonPin, INPUT_PULLUP);
    pinMode(moteurPin, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);

    digitalWrite(moteurPin, LOW);

    if (radio.begin()) {
        radio.setChannel(108);
        radio.openWritingPipe(adresse);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setAutoAck(false); // Pas d'ACK nécessaire
        radio.stopListening();

        Serial.print("Buzzer Equipe ");
        Serial.print(monEquipe);
        Serial.println(" pret !");
    } else {
        Serial.println("ERREUR : Module Radio non detecte !");
    }
}

// =========================================================
// LOOP
// =========================================================
void loop() {
    if (digitalRead(boutonPin) == LOW) {

        // 1. Vibration + LED immédiate
        digitalWrite(moteurPin, HIGH);
        digitalWrite(LED_BUILTIN, HIGH);
        Serial.println("Appui detecte, envoi du signal...");

        // 2. Envoyer le numero d'equipe au meme format binaire que la Mega lit
        int signal = monEquipe;
        bool succes = radio.write(&signal, sizeof(signal));

        // 3. Vibration 200ms
        delay(200);
        digitalWrite(moteurPin, LOW);
        digitalWrite(LED_BUILTIN, LOW);

        if (succes) {
            Serial.print("TRANSMIS : Equipe ");
            Serial.print(monEquipe);
            Serial.println(" envoyee !");
        } else {
            Serial.println("Signal envoye !");
            // Note : succes peut être false même si la Mega reçoit
            // car AutoAck est désactivé - ce n'est pas une vraie erreur
        }

        // 4. Anti-spam + attendre relâchement
        delay(1000);
        while (digitalRead(boutonPin) == LOW);
    }
}
