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

// --- ID DE L'EQUIPE (1 a 30) ---
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
const unsigned long FEEDBACK_DURATION_MS = 120; // Durée d'activation du vibreur/LED

// [FIX] Jitter aléatoire avant l'émission radio, pour éviter que plusieurs équipes
// qui buzzent au même instant ne retentent aussi de façon synchronisée (l'ancien
// code n'avait aucune vraie randomisation : setRetries() utilise un délai FIXE,
// et la boucle de réessai manuelle aussi — donc plusieurs Nanos identiques
// pouvaient rester en collision répétée). 0-15ms suffit à casser la synchronisation
// sans ralentir sensiblement la réactivité perçue par le joueur.
#define JITTER_MAX_MS 15

unsigned long lastSendMs = 0;
bool lastBtn = true;
unsigned long lastChangeMs = 0;

bool feedbackActif = false;
unsigned long feedbackStartMs = 0;

// =========================================================
// SETUP
// =========================================================
void setup() {
    Serial.begin(9600);
    pinMode(boutonPin, INPUT_PULLUP);
    pinMode(moteurPin, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);

    digitalWrite(moteurPin, LOW);
    wdt_enable(WDTO_2S); // Watchdog 2 secondes

    // [FIX] Graine aléatoire propre à cette carte : combine le bruit analogique d'une
    // broche non connectée (A0) avec le numéro d'équipe, pour garantir que deux
    // Nanos ne tirent jamais exactement la même séquence "aléatoire".
    // Vérifie que A0 est bien inutilisée sur ton montage avant de garder cette ligne.
    randomSeed(analogRead(A0) + monEquipe);

    if (radio.begin()) {
        radio.setChannel(RADIO_CHANNEL);
        radio.setAddressWidth(5);
        radio.openWritingPipe(adresse);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setCRCLength(RF24_CRC_16);
        radio.setPayloadSize(sizeof(RadioMsg));

        // Fiabilité maximale
        radio.setAutoAck(true);
        radio.setRetries(10, 15); // 10 essais, 3750µs d'attente entre chaque
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
    wdt_reset(); // Reset du Watchdog à chaque tour
    unsigned long now = millis();

    // -----------------------------------------------------
    // GESTION NON-BLOQUANTE DU FEEDBACK (Vibreur / LED)
    // -----------------------------------------------------
    if (feedbackActif && (now - feedbackStartMs >= FEEDBACK_DURATION_MS)) {
        digitalWrite(moteurPin, LOW);
        digitalWrite(LED_BUILTIN, LOW);
        feedbackActif = false;
    }

    // -----------------------------------------------------
    // LECTURE DU BOUTON & DEBOUNCE
    // -----------------------------------------------------
    bool btn = (digitalRead(boutonPin) == LOW);

    if (btn != lastBtn) {
        lastBtn = btn;
        lastChangeMs = now;
    }

    // Appui stable détecté
    if (btn && (now - lastChangeMs) >= DEBOUNCE_MS) {
        // Vérification du Cooldown pour éviter le spam
        if (now - lastSendMs >= COOLDOWN_MS) {
            lastSendMs = now;

            // Déclenchement du feedback immédiat (sans bloquer la loop)
            // [FIX] Le feedback (vibreur/LED) reste immédiat, AVANT le jitter :
            // le joueur ressent une réponse instantanée même si l'émission radio
            // elle-même est légèrement décalée.
            digitalWrite(moteurPin, HIGH);
            digitalWrite(LED_BUILTIN, HIGH);
            feedbackActif = true;
            feedbackStartMs = now;

            // [FIX] Jitter aléatoire avant l'émission radio (voir JITTER_MAX_MS plus haut)
            delay(random(0, JITTER_MAX_MS + 1));

            // Préparation du message
            RadioMsg msg;
            msg.kind = 1;
            msg.value = (uint8_t)monEquipe;
            msg.seq = ++seqCounter;

            // Envoi Radio avec retry manuel si l'AutoAck échoue du premier coup
            bool ok = false;
            for (int i = 0; i < 3 && !ok; i++) { // Réduit à 3 tentatives max (l'AutoAck fait déjà 10 essais en interne)
                ok = radio.write(&msg, sizeof(msg));
                if (!ok && i < 2) {
                    // [FIX] Petit jitter aléatoire ici aussi (au lieu d'un délai fixe),
                    // pour ne pas retomber en synchronisation sur les tentatives suivantes.
                    delayMicroseconds(random(200, 800));
                }
            }

            // Log de debug
            if (ok) {
                Serial.print("Buzz envoye par l'equipe ");
                Serial.println(monEquipe);
            } else {
                Serial.println("Echec de la transmission radio (Ack non recu)");
            }
        }
    }
}
