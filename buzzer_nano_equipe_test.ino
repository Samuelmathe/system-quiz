#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <avr/wdt.h>

// =========================================================
// STRESS TEST — envoie des buzz automatiquement en rafale
// aleatoire (1-4s) pour tester la robustesse reception du MEGA.
// Base sur buzzer_nano_equipe.ino, bouton remplace par un
// timer aleatoire. Meme protocole radio => compatible direct
// avec le recepteur MEGA existant.
// =========================================================
RF24 radio(9, 10); // CE sur 9, CSN sur 10
const byte adresse[6] = "00001";
const int moteurPin  = 3; // Vibreur (feedback visuel du buzz envoye)

#define RADIO_CHANNEL  108

// --- ID DE L'EQUIPE (1 a 30) ---
// Change cette valeur (ou branche plusieurs Nanos avec des IDs differents)
// pour simuler plusieurs equipes qui spamment le Mega en meme temps.
int monEquipe = 3;

// Message radio (doit matcher la MEGA)
struct RadioMsg {
    uint8_t kind;   // 1=BUZZ_EQUIPE
    uint8_t value;  // team 1..30
    uint16_t seq;   // increment
};
uint16_t seqCounter = 0;

const unsigned long FEEDBACK_DURATION_MS = 120; // Duree d'activation du vibreur/LED

// [STRESS] Fenetre resserree (100-300ms) pour forcer un vrai taux de
// collisions radio entre plusieurs Nanos (au lieu de 1-4s qui donne un
// trafic trop etale pour vraiment stresser le Mega). Avec 7 equipes ~35
// buzz/s combines => proba de collision par buzz ~17%.
#define BUZZ_INTERVAL_MIN_MS 100
#define BUZZ_INTERVAL_MAX_MS 300

// [DIAGNOSTIC] Voir le meme commentaire dans rf_nano_bridge.ino : PA_MAX
// a quelques cm entre 8 nRF24 peut saturer/desensibiliser le recepteur.
// Baisse temporaire pour isoler cette hypothese sur le figement du pont.
#define RADIO_PA_LEVEL RF24_PA_LOW

unsigned long prochainBuzzMs = 0;

bool feedbackActif = false;
unsigned long feedbackStartMs = 0;

// =========================================================
// SETUP
// =========================================================
void setup() {
    Serial.begin(9600);
    pinMode(moteurPin, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);

    digitalWrite(moteurPin, LOW);
    wdt_enable(WDTO_2S); // Watchdog 2 secondes

    randomSeed(analogRead(A0) + monEquipe);

    if (radio.begin()) {
        radio.setChannel(RADIO_CHANNEL);
        radio.setAddressWidth(5);
        radio.openWritingPipe(adresse);
        radio.setPALevel(RADIO_PA_LEVEL);
        radio.setDataRate(RF24_250KBPS);
        radio.setCRCLength(RF24_CRC_16);
        radio.setPayloadSize(sizeof(RadioMsg));

        radio.setAutoAck(true);
        radio.setRetries(10, 15);
        radio.stopListening();

        Serial.print("STRESS TEST - Buzzer Equipe ");
        Serial.print(monEquipe);
        Serial.println(" pret ! Envoi automatique toutes les 100-300ms.");
    } else {
        Serial.println("ERREUR : Module Radio non detecte !");
    }

    planifierProchainBuzz();
}

void planifierProchainBuzz() {
    unsigned long intervalle = random(BUZZ_INTERVAL_MIN_MS, BUZZ_INTERVAL_MAX_MS + 1);
    prochainBuzzMs = millis() + intervalle;
}

// =========================================================
// LOOP
// =========================================================
void loop() {
    wdt_reset();
    unsigned long now = millis();

    // Fin du feedback vibreur/LED (non-bloquant)
    if (feedbackActif && (now - feedbackStartMs >= FEEDBACK_DURATION_MS)) {
        digitalWrite(moteurPin, LOW);
        digitalWrite(LED_BUILTIN, LOW);
        feedbackActif = false;
    }

    // Declenchement automatique du buzz
    if ((long)(now - prochainBuzzMs) >= 0) {
        digitalWrite(moteurPin, HIGH);
        digitalWrite(LED_BUILTIN, HIGH);
        feedbackActif = true;
        feedbackStartMs = now;

        RadioMsg msg;
        msg.kind = 1;
        msg.value = (uint8_t)monEquipe;
        msg.seq = ++seqCounter;

        bool ok = false;
        for (int i = 0; i < 3 && !ok; i++) {
            ok = radio.write(&msg, sizeof(msg));
            if (!ok && i < 2) {
                delayMicroseconds(random(200, 800));
            }
        }

        Serial.print("[");
        Serial.print(now);
        Serial.print("ms] Buzz #");
        Serial.print(msg.seq);
        Serial.print(" equipe ");
        Serial.print(monEquipe);
        Serial.println(ok ? " -> OK" : " -> ECHEC (Ack non recu)");

        planifierProchainBuzz();
    }
}
