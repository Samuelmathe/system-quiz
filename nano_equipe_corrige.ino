#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <avr/wdt.h>

// =========================================================
// CONFIGURATION
// =========================================================
RF24 radio(9, 10); // CE sur 9, CSN sur 10
const byte adresse[6] = "00001";
const int boutonPin  = 2;
const int moteurPin  = 3; // Vibreur

#define RADIO_CHANNEL  108

// --- ID DE L'EQUIPE (modifier de 1 a 8) ---
int monEquipe = 3;

// Message radio (doit matcher la MEGA)
struct RadioMsg {
    uint8_t kind;   // 1=BUZZ_EQUIPE
    uint8_t value;  // team 1..30
    uint16_t seq;   // increment
};
uint16_t seqCounter = 0;

// Anti-rebond / anti-spam (ms)
const unsigned long DEBOUNCE_MS = 25;
const unsigned long COOLDOWN_MS = 250;
unsigned long lastSendMs = 0;
bool lastBtn = true;
unsigned long lastChangeMs = 0;

// =========================================================
// SETUP
// =========================================================
void setup() {
    Serial.begin(9600);
    pinMode(boutonPin, INPUT_PULLUP);
    pinMode(moteurPin, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);

    digitalWrite(moteurPin, LOW);
    wdt_enable(WDTO_2S);

    if (radio.begin()) {
        radio.setChannel(RADIO_CHANNEL);
        radio.setAddressWidth(5);
        radio.openWritingPipe(adresse);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setCRCLength(RF24_CRC_16);
        radio.setPayloadSize(sizeof(RadioMsg));
        // Fiabilité: AutoAck + retries
        radio.setAutoAck(true);
        radio.setRetries(10, 15);
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
    wdt_reset();
    bool btn = (digitalRead(boutonPin) == LOW);
    unsigned long now = millis();

    if (btn != lastBtn) {
        lastBtn = btn;
        lastChangeMs = now;
    }

    // Appui stable (debounce)
    if (btn && (now - lastChangeMs) >= DEBOUNCE_MS) {
        // Cooldown pour éviter spam + garder la réactivité
        if (now - lastSendMs >= COOLDOWN_MS) {
            lastSendMs = now;

            // Feedback immédiat
            digitalWrite(moteurPin, HIGH);
            digitalWrite(LED_BUILTIN, HIGH);

            int signal = monEquipe;

            RadioMsg msg;
            msg.kind = 1;
            msg.value = (uint8_t)signal;
            msg.seq = ++seqCounter;

            // Essayer plusieurs fois: avec AutoAck, on s'arrête dès que ça passe
            bool ok = false;
            for (int i = 0; i < 5 && !ok; i++) {
                ok = radio.write(&msg, sizeof(msg));
                if (!ok) delay(8);
            }

            delay(120);
            digitalWrite(moteurPin, LOW);
            digitalWrite(LED_BUILTIN, LOW);
        }
    }
}
