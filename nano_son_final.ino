#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// =========================================================
// CONFIGURATION
// =========================================================
RF24 radio(9, 10);

// Adresse dédiée Nano son
// Reçoit UNIQUEMENT les signaux de la Mega !
// Jamais les signaux des buzzers équipes !
const byte adresseSon[6] = "00002";

// DFPlayer sur broches D4 (TX) et D5 (RX)
SoftwareSerial mySoftwareSerial(5, 4); // RX=D5, TX=D4
DFRobotDFPlayerMini monLecteurMP3;

// =========================================================
// FICHIERS MP3 SUR CARTE SD
// =========================================================
// mp3/0001.mp3 → Son BUZZ équipe
// mp3/0002.mp3 → Son VICTOIRE (bonne réponse)
// mp3/0003.mp3 → Son ÉCHEC (mauvaise réponse)

// =========================================================
// VARIABLES
// =========================================================
bool radioOK = false;
bool mp3OK   = false;
unsigned long dernierBattement = 0;

// =========================================================
// SETUP
// =========================================================
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);

    // 1. DFPlayer alimenté en 3.3V via SoftwareSerial
    mySoftwareSerial.begin(9600);
    if (monLecteurMP3.begin(mySoftwareSerial, false, false)) {
        mp3OK = true;
        monLecteurMP3.setTimeOut(500);
        monLecteurMP3.volume(25);
        monLecteurMP3.EQ(DFPLAYER_EQ_NORMAL);
    }

    // 2. RADIO - écoute uniquement adresse "00002"
    if (radio.begin()) {
        radioOK = true;
        radio.setChannel(108);
        radio.setPALevel(RF24_PA_MAX);
        radio.setDataRate(RF24_250KBPS);
        radio.setAutoAck(false);
        radio.openReadingPipe(1, adresseSon);
        radio.startListening();

        // Radio OK -> 3 clignotements
        for (int i = 0; i < 3; i++) {
            digitalWrite(LED_BUILTIN, HIGH); delay(100);
            digitalWrite(LED_BUILTIN, LOW);  delay(100);
        }
    } else {
        // Radio KO -> LED fixe 2 secondes
        digitalWrite(LED_BUILTIN, HIGH);
        delay(2000);
        digitalWrite(LED_BUILTIN, LOW);
    }

    // Vider buffer radio
    delay(100);
    int poubelle;
    while (radio.available()) { radio.read(&poubelle, sizeof(poubelle)); }
}

// =========================================================
// LOOP
// =========================================================
void loop() {

    // BATTEMENT LED toutes les 2 secondes si radio OK
    if (radioOK && millis() - dernierBattement > 2000) {
        digitalWrite(LED_BUILTIN, HIGH); delay(50);
        digitalWrite(LED_BUILTIN, LOW);
        dernierBattement = millis();
    }

    // RÉCEPTION RADIO (uniquement depuis la Mega)
    if (radioOK && radio.available()) {
        int signal = 0;
        radio.read(&signal, sizeof(signal));

        // Clignotement = signal reçu
        digitalWrite(LED_BUILTIN, HIGH); delay(50);
        digitalWrite(LED_BUILTIN, LOW);

        if (!mp3OK) return;

        // =====================================================
        // 200 = Buzz valide → 0001.mp3
        // =====================================================
        if (signal == 200) {
            monLecteurMP3.play(1);
        }

        // =====================================================
        // 201 = Bonne réponse → 0002.mp3
        // =====================================================
        else if (signal == 201) {
            monLecteurMP3.play(2);
        }

        // =====================================================
        // 202 = Mauvaise réponse → 0003.mp3
        // =====================================================
        else if (signal == 202) {
            monLecteurMP3.play(3);
        }
    }
}
