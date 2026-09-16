// =========================================================
// BUZZER EQUIPE — VERSION ESP32 / ESP-NOW (SECOURS DU NANO+nRF24)
// =========================================================
// A UTILISER UNIQUEMENT si buzzer_nano_equipe.ino (Nano + nRF24) pose
// probleme sur site (module nRF24 defaillant, interference...). Remplace
// le boitier d'UNE equipe sans rien changer aux autres : le hub
// (esp32/esp32_bridge_server.ino) ecoute nRF24 ET ESP-NOW en meme temps,
// avec le meme dedoublonnage/fenetre d'arbitrage cote hub.
//
// Carte : n'importe quel ESP32 (ESP32 Dev Module recommande, meme carte
// que le hub). Aucun module radio externe : le Wi-Fi/ESP-NOW est integre
// a la puce.
//
// Cablage :
//   - Bouton  : GPIO 4 -> GND (pull-up interne, pas de resistance externe)
//   - LED     : GPIO 2 (LED integree sur la plupart des cartes ESP32 Dev Module)
//   - Vibreur : GPIO 13 (optionnel, cable par le concepteur du boitier -- comme
//               le moteurPin de buzzer_nano_equipe.ino)
//
// IMPORTANT : ESPNOW_WIFI_CHANNEL ci-dessous DOIT etre identique a celui
// du hub (esp32_bridge_server.ino). Si vous changez l'un, changez l'autre.
// =========================================================

#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"

// --- ID DE L'EQUIPE (1 a 30) ---
int monEquipe = 3;

// --- Configuration ESP-NOW ---
#define ESPNOW_WIFI_CHANNEL 6  // Doit matcher esp32_bridge_server.ino
uint8_t adresseBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

const int boutonPin = 4;
const int ledPin    = 2;
const int moteurPin = 13; // vibreur optionnel

// Message radio (doit matcher esp32_bridge_server.ino ET les Nanos existants)
struct RadioMsg {
    uint8_t kind;   // 1 = BUZZ_EQUIPE
    uint8_t value;  // team 1..30
    uint16_t seq;   // increment
};
uint16_t seqCounter = 0;

// Anti-rebond / anti-spam (ms) — memes valeurs que buzzer_nano_equipe.ino
const unsigned long DEBOUNCE_MS = 25;
const unsigned long COOLDOWN_MS = 250;
const unsigned long FEEDBACK_DURATION_MS = 120;

// [FIX] Jitter aleatoire avant l'emission, meme raison que sur le Nano nRF24 :
// eviter que plusieurs equipes qui buzzent au meme instant collisionnent de
// facon synchronisee. Le Wi-Fi gere deja une part de ca au niveau MAC
// (CSMA/CA), mais le jitter applicatif reste utile en cas de buzz simultanes.
#define JITTER_MAX_MS 15

// [FIX] Le broadcast ESP-NOW n'a PAS d'accuse de reception materiel
// (contrairement au nRF24 avec setAutoAck(true)) : impossible de savoir si
// le hub a bien recu un paquet broadcast. On compense en envoyant une petite
// RAFALE de copies identiques (meme seq) au lieu d'un seul paquet + retry --
// exactement le meme principe que la rafale son deja utilisee dans ce projet
// (voir sonBurstN/sonGapMs dans esp32_bridge_server.ino) : le hub dedoublonne
// par seq, donc les copies en trop ne creent jamais de buzz en double.
#define BURST_COUNT 4
#define BURST_GAP_MS 12

unsigned long lastSendMs = 0;
bool lastBtn = true;
unsigned long lastChangeMs = 0;

bool feedbackActif = false;
unsigned long feedbackStartMs = 0;

bool espNowOK = false;

bool initEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) return false;

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, adresseBroadcast, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    if (!esp_now_is_peer_exist(adresseBroadcast)) {
        if (esp_now_add_peer(&peerInfo) != ESP_OK) return false;
    }
    return true;
}

void envoyerBuzz() {
    RadioMsg msg;
    msg.kind = 1;
    msg.value = (uint8_t)monEquipe;
    msg.seq = ++seqCounter;

    for (uint8_t i = 0; i < BURST_COUNT; i++) {
        esp_now_send(adresseBroadcast, (uint8_t*)&msg, sizeof(msg));
        if (i < BURST_COUNT - 1) delay(BURST_GAP_MS);
    }

    Serial.print("Buzz envoye (ESP-NOW) par l'equipe ");
    Serial.println(monEquipe);
}

void setup() {
    Serial.begin(115200);
    pinMode(boutonPin, INPUT_PULLUP);
    pinMode(moteurPin, OUTPUT);
    pinMode(ledPin, OUTPUT);
    digitalWrite(moteurPin, LOW);
    digitalWrite(ledPin, LOW);

    randomSeed(analogRead(A0) + monEquipe);

    espNowOK = initEspNow();
    if (espNowOK) {
        Serial.print("Buzzer Equipe ");
        Serial.print(monEquipe);
        Serial.println(" (ESP-NOW) pret !");
    } else {
        Serial.println("ERREUR : init ESP-NOW impossible !");
    }
}

void loop() {
    unsigned long now = millis();

    // Retente periodiquement si l'init a echoue au demarrage
    static unsigned long prochainRetryMs = 0;
    if (!espNowOK && (long)(now - prochainRetryMs) >= 0) {
        prochainRetryMs = now + 2000;
        espNowOK = initEspNow();
    }

    // Feedback non-bloquant (LED/vibreur)
    if (feedbackActif && (now - feedbackStartMs >= FEEDBACK_DURATION_MS)) {
        digitalWrite(moteurPin, LOW);
        digitalWrite(ledPin, LOW);
        feedbackActif = false;
    }

    // Lecture bouton + debounce
    bool btn = (digitalRead(boutonPin) == LOW);
    if (btn != lastBtn) {
        lastBtn = btn;
        lastChangeMs = now;
    }

    if (btn && (now - lastChangeMs) >= DEBOUNCE_MS) {
        if (now - lastSendMs >= COOLDOWN_MS) {
            lastSendMs = now;

            // Feedback immediat, avant le jitter/la rafale (reactivite ressentie)
            digitalWrite(moteurPin, HIGH);
            digitalWrite(ledPin, HIGH);
            feedbackActif = true;
            feedbackStartMs = now;

            delay(random(0, JITTER_MAX_MS + 1));

            if (espNowOK) {
                envoyerBuzz();
            } else {
                Serial.println("Buzz ignore : ESP-NOW non initialise");
            }
        }
    }
}
