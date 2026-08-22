#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <avr/wdt.h>

// =========================================================
// BURST SYNCHRONISE — force une vraie collision radio a N
// equipes en meme temps (pire cas reel d'un quiz : tout le
// monde buzz au meme instant).
//
// Principe : PAS de synchro d'horloge (les Nanos ne bootent
// jamais exactement au meme moment, donc "prochain multiple
// de Xs depuis le boot" ne s'aligne jamais entre cartes).
// A la place, on utilise le radio lui-meme :
//   - UN Nano en ROLE_MASTER a un BOUTON POUSSOIR : quand l'operateur
//     appuie, il diffuse un signal "GO" sur un pipe dedie, puis
//     envoie aussitot son propre buzz au Mega.
//   - Les Nanos en ROLE_SLAVE restent a l'ecoute de ce pipe et,
//     des reception du GO, envoient leur buzz au Mega dans la
//     foulee.
// => tous les buzz partent a quelques centaines de µs / 1-2ms
//    d'ecart les uns des autres, peu importe l'heure de boot
//    de chaque carte. C'est ca qui force la vraie collision, au
//    moment choisi par l'operateur (comme un vrai round de quiz).
//
// EN PLUS : chaque Nano (maitre ET esclaves) envoie AUSSI ses propres
// buzz aleatoires en continu (toutes les BUZZ_INTERVAL_MIN/MAX_MS),
// exactement comme buzzer_nano_equipe_test.ino. Ca cree un bruit de
// fond permanent sur lequel viennent s'ajouter les collisions a 7
// forcees par le bouton du maitre.
//
// A FLASHER : 1 seul Nano avec MON_ROLE = ROLE_MASTER (avec un
// bouton poussoir sur boutonPin), les autres (jusqu'a 6) avec
// MON_ROLE = ROLE_SLAVE, chacun avec un monEquipe different (1..7).
// =========================================================
RF24 radio(9, 10); // CE sur 9, CSN sur 10

const byte adresseBuzz[6] = "00001"; // Nano -> Mega (identique au buzzer normal)
const byte adresseSync[6] = "SYNC1"; // Maitre -> Esclaves (signal GO)

const int moteurPin = 3; // Vibreur (feedback visuel du buzz envoye)
const int boutonPin = 2; // Bouton poussoir de declenchement (MAITRE uniquement)

#define RADIO_CHANNEL  108

// --- ROLE de cette carte ---
#define ROLE_MASTER 1
#define ROLE_SLAVE  0
#define MON_ROLE ROLE_SLAVE   // <-- Mettre ROLE_MASTER sur UN SEUL Nano !

// --- ID DE L'EQUIPE (1 a 30), doit etre unique par Nano ---
int monEquipe = 1;

// Anti-rebond / anti-repetition du bouton (MAITRE uniquement)
const unsigned long DEBOUNCE_MS = 25;
const unsigned long COOLDOWN_MS = 300; // empeche de spammer le GO si le bouton reste appuye

// Rafale de buzz aleatoires en continu, sur CHAQUE Nano (maitre + esclaves)
#define BUZZ_INTERVAL_MIN_MS 100
#define BUZZ_INTERVAL_MAX_MS 300

// [DIAGNOSTIC] Voir le meme commentaire dans rf_nano_bridge.ino : PA_MAX
// a quelques cm entre 8 nRF24 peut saturer/desensibiliser le recepteur.
// Baisse temporaire pour isoler cette hypothese sur le figement du pont.
#define RADIO_PA_LEVEL RF24_PA_LOW

// Message radio (doit matcher la MEGA pour le buzz)
struct RadioMsg {
    uint8_t kind;   // 1=BUZZ_EQUIPE, 99=GO (signal de synchro, jamais envoye au Mega)
    uint8_t value;  // team 1..30 (ou inutilise pour GO)
    uint16_t seq;   // increment
};
uint16_t seqCounterBuzz = 0;
uint16_t seqCounterGo = 0;

const unsigned long FEEDBACK_DURATION_MS = 120;

// Etat bouton (maitre seulement)
unsigned long lastSendMs = 0;
bool lastBtn = true;
unsigned long lastChangeMs = 0;

// Rafale aleatoire (maitre + esclaves)
unsigned long prochainBuzzMs = 0;

bool feedbackActif = false;
unsigned long feedbackStartMs = 0;

void planifierProchainBuzz() {
    unsigned long intervalle = random(BUZZ_INTERVAL_MIN_MS, BUZZ_INTERVAL_MAX_MS + 1);
    prochainBuzzMs = millis() + intervalle;
}

// =========================================================
// ENVOI D'UN BUZZ AU MEGA (commun maitre/esclave)
// =========================================================
void envoyerBuzz() {
    digitalWrite(moteurPin, HIGH);
    digitalWrite(LED_BUILTIN, HIGH);
    feedbackActif = true;
    feedbackStartMs = millis();

    radio.stopListening();
    radio.openWritingPipe(adresseBuzz);

    RadioMsg msg;
    msg.kind = 1;
    msg.value = (uint8_t)monEquipe;
    msg.seq = ++seqCounterBuzz;

    bool ok = false;
    for (int i = 0; i < 3 && !ok; i++) {
        ok = radio.write(&msg, sizeof(msg));
        if (!ok && i < 2) {
            delayMicroseconds(random(200, 800));
        }
    }

    Serial.print("[");
    Serial.print(millis());
    Serial.print("ms] Buzz #");
    Serial.print(msg.seq);
    Serial.print(" equipe ");
    Serial.print(monEquipe);
    Serial.println(ok ? " -> OK" : " -> ECHEC (Ack non recu)");

    // Esclave : on repasse en ecoute du signal GO pour le prochain round.
    // Maitre : reste en TX, il gere lui-meme son cycle dans loop().
#if MON_ROLE == ROLE_SLAVE
    radio.openReadingPipe(1, adresseSync);
    radio.startListening();
#endif

    // Reprogramme le prochain buzz aleatoire (rafale de bruit de fond),
    // que ce buzz-ci vienne du GO/bouton ou de la rafale elle-meme.
    planifierProchainBuzz();
}

// =========================================================
// SETUP
// =========================================================
void setup() {
    Serial.begin(9600);
    pinMode(moteurPin, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(moteurPin, LOW);
#if MON_ROLE == ROLE_MASTER
    pinMode(boutonPin, INPUT_PULLUP);
#endif

    wdt_enable(WDTO_2S);
    randomSeed(analogRead(A0) + monEquipe);

    if (!radio.begin()) {
        Serial.println("ERREUR : Module Radio non detecte !");
        return;
    }

    radio.setChannel(RADIO_CHANNEL);
    radio.setAddressWidth(5);
    radio.setPALevel(RADIO_PA_LEVEL);
    radio.setDataRate(RF24_250KBPS);
    radio.setCRCLength(RF24_CRC_16);
    radio.setPayloadSize(sizeof(RadioMsg));
    radio.setAutoAck(true);
    radio.setRetries(10, 15);

#if MON_ROLE == ROLE_MASTER
    radio.openWritingPipe(adresseBuzz);
    radio.stopListening();
    Serial.print("BURST SYNC - MAITRE, equipe ");
    Serial.print(monEquipe);
    Serial.println(" - rafale aleatoire en continu + bouton pour declencher le burst a 7");
#else
    radio.openReadingPipe(1, adresseSync);
    radio.startListening();
    Serial.print("BURST SYNC - ESCLAVE, equipe ");
    Serial.print(monEquipe);
    Serial.println(" - rafale aleatoire en continu + en attente du signal GO...");
#endif

    planifierProchainBuzz();
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

#if MON_ROLE == ROLE_MASTER
    // Le maitre declenche le burst a 7 sur appui du bouton poussoir.
    bool btn = (digitalRead(boutonPin) == LOW);

    if (btn != lastBtn) {
        lastBtn = btn;
        lastChangeMs = now;
    }

    if (btn && (now - lastChangeMs) >= DEBOUNCE_MS) {
        if (now - lastSendMs >= COOLDOWN_MS) {
            lastSendMs = now;

            RadioMsg go;
            go.kind = 99;
            go.value = 0;
            go.seq = ++seqCounterGo;

            radio.stopListening();
            radio.openWritingPipe(adresseSync);
            radio.write(&go, sizeof(go), true); // multicast = true => pas d'ACK attendu (plusieurs esclaves ecoutent la meme adresse)

            envoyerBuzz(); // remet openWritingPipe sur adresseBuzz et transmet au Mega
        }
    }

    // + rafale aleatoire en continu (bruit de fond), independante du bouton.
    if ((long)(now - prochainBuzzMs) >= 0) {
        envoyerBuzz();
    }
#else
    // L'esclave ecoute le signal GO et reagit immediatement.
    if (radio.available()) {
        RadioMsg go;
        radio.read(&go, sizeof(go));
        if (go.kind == 99) {
            envoyerBuzz();
        }
    }

    // + rafale aleatoire en continu (bruit de fond), independante du GO.
    // envoyerBuzz() bascule temporairement en TX puis repasse en ecoute a la fin.
    if ((long)(now - prochainBuzzMs) >= 0) {
        envoyerBuzz();
    }
#endif
}
