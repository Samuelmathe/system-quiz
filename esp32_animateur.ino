// =========================================================
// TELECOMMANDE ANIMATEUR — VERSION ESP32 / ESP-NOW (SECOURS DU NANO+nRF24)
// =========================================================
// A UTILISER UNIQUEMENT si nano_animateur_final.ino (Nano + nRF24) pose
// probleme sur site. Le hub (esp32/esp32_bridge_server.ino) ecoute nRF24 ET
// ESP-NOW en meme temps : ce boitier peut remplacer le Nano animateur sans
// rien changer au hub ni aux boitiers equipes.
//
// 2 boutons :
//   GPIO 4 = VALIDER  (bonne reponse) -> envoie CMD 99 (RESET_ALL)
//   GPIO 5 = REFUSER  (mauvaise reponse) -> envoie CMD 88 (RELANCE_PARTIEL)
//
// Cablage boutons : GPIO -> bouton -> GND (pull-up interne, pas de
// resistance externe necessaire). LED integree sur GPIO 2.
//
// IMPORTANT : ESPNOW_WIFI_CHANNEL ci-dessous DOIT etre identique a celui
// du hub (esp32_bridge_server.ino) et de esp32_buzzer_equipe.ino.
// =========================================================

#include <WiFi.h>
#include <esp_now.h>
#include "esp_wifi.h"

#define ESPNOW_WIFI_CHANNEL 6  // Doit matcher esp32_bridge_server.ino
uint8_t adresseBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#define PIN_VALIDER 4
#define PIN_REFUSER 5
#define LED 2

#define DEBOUNCE_MS 250

// [FIX] Meme compensation d'absence d'accuse de reception broadcast que
// esp32_buzzer_equipe.ino : rafale de copies au lieu d'un ACK materiel,
// dedoublonnee par seq cote hub (voir commentaire equivalent la-bas).
#define BURST_COUNT 4
#define BURST_GAP_MS 12

struct RadioMsg {
    uint8_t kind;   // 2 = CMD
    uint8_t value;  // 99 = RESET_ALL, 88 = RELANCE_PARTIEL
    uint16_t seq;
};

static uint16_t seqCounter = 0;
unsigned long dernierAppuiValider = 0;
unsigned long dernierAppuiRefuser = 0;
bool espNowOK = false;

void blinkPattern(uint8_t nBlinks, unsigned long onMs, unsigned long offMs) {
    for (uint8_t i = 0; i < nBlinks; i++) {
        digitalWrite(LED, HIGH);
        delay(onMs);
        digitalWrite(LED, LOW);
        delay(offMs);
    }
}

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

void envoyerCommande(uint8_t value) {
    RadioMsg msg;
    msg.kind = 2;
    msg.value = value;
    msg.seq = ++seqCounter;

    for (uint8_t i = 0; i < BURST_COUNT; i++) {
        esp_now_send(adresseBroadcast, (uint8_t*)&msg, sizeof(msg));
        if (i < BURST_COUNT - 1) delay(BURST_GAP_MS);
    }

    blinkPattern(1, 150, 0); // 1 clignotement = commande envoyee
}

void setup() {
    Serial.begin(115200);
    pinMode(LED, OUTPUT);
    pinMode(PIN_VALIDER, INPUT_PULLUP);
    pinMode(PIN_REFUSER, INPUT_PULLUP);

    espNowOK = initEspNow();
    if (espNowOK) {
        Serial.println("Animateur (ESP-NOW) pret !");
        blinkPattern(2, 80, 80);
    } else {
        Serial.println("ERREUR : init ESP-NOW impossible !");
        blinkPattern(5, 60, 60);
    }
}

void loop() {
    unsigned long now = millis();

    static unsigned long prochainRetryMs = 0;
    if (!espNowOK && (long)(now - prochainRetryMs) >= 0) {
        prochainRetryMs = now + 2000;
        espNowOK = initEspNow();
    }

    if (espNowOK && digitalRead(PIN_VALIDER) == LOW && now - dernierAppuiValider > DEBOUNCE_MS) {
        dernierAppuiValider = now;
        envoyerCommande(99); // VALIDER -> RESET_ALL
    }

    if (espNowOK && digitalRead(PIN_REFUSER) == LOW && now - dernierAppuiRefuser > DEBOUNCE_MS) {
        dernierAppuiRefuser = now;
        envoyerCommande(88); // REFUSER -> RELANCE_PARTIEL
    }
}
