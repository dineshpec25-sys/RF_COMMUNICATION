/*
 * VILLAGE — Final Code with Physical Buttons
 * - Button 1 (GPIO 16) → sends "All clear at village" to RANGER
 * - Button 2 (GPIO 17) → sends "Need help at village" to RANGER
 * - Receives keyword from RANGER → looks up message → scrolls on matrix
 *
 * Button wiring (DIP button):
 *   Pin 1 → GPIO 16 or 17
 *   Pin 2 → GND
 *   (internal pull-up enabled — no resistor needed)
 */

#include <SPI.h>
#include <LoRa.h>
#include <MD_Parola.h>
#include <MD_MAX72xx.h>

// ── LoRa pins ────────────────────────────────────────────────
#define LORA_SS   5
#define LORA_RST  14
#define LORA_DIO0 2

// ── Matrix pins ───────────────────────────────────────────────
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES   4
#define CLK_PIN       12
#define DATA_PIN      13
#define CS_PIN        15

// ── Button pins ───────────────────────────────────────────────
#define BTN1_PIN  16    // "All clear at village"
#define BTN2_PIN  17    // "Need help at village"

// ── Button messages ───────────────────────────────────────────
const char* BTN1_MSG = "All clear at village";
const char* BTN2_MSG = "Need help at village";

// ── Message dictionary ───────────────────────────────────────
struct Entry { const char *keyword; const char *message; };

const Entry dictionary[] = {
  { "weather",  "Clear skies. No rain expected today." },
  { "water",    "River level normal. Safe to cross." },
  { "fire",     "FIRE ALERT! Evacuate north side now!" },
  { "supply",   "Supply drop at checkpoint 3 at 1400h." },
  { "medical",  "Medic on standby at base camp." },
  { "safe",     "All units safe. No threats detected." },
  { "danger",   "DANGER ZONE - Stay away from sector 7!" },
  { "food",     "Food rations distributed until noon." },
  { "signal",   "Signal relay active. Channel 3 open." },
  { "base",     "Base camp secure. Return when ready." },
};
const int DICT_SIZE = sizeof(dictionary) / sizeof(dictionary[0]);

// ── Globals ───────────────────────────────────────────────────
MD_Parola matrix = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

static char displayBuf[128] = "VILLAGE READY";
static char pendingBuf[128] = "";
bool newMessagePending = false;

// Button debounce
bool btn1Last = HIGH;
bool btn2Last = HIGH;
unsigned long btn1Time = 0;
unsigned long btn2Time = 0;
#define DEBOUNCE_MS 200

// ────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Buttons — internal pull-up
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);

  // Matrix
  matrix.begin();
  matrix.setIntensity(5);
  matrix.displayText(displayBuf, PA_LEFT, 50, 500, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  Serial.println("Matrix OK");

  // LoRa
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa FAILED!");
    while (true);
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);

  Serial.println("VILLAGE READY");
  Serial.println("BTN1 → All clear at village");
  Serial.println("BTN2 → Need help at village");
}

// ────────────────────────────────────────────────────────────
void sendToRanger(const char* message) {
  String packet = "MSG:" + String(message);
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
  Serial.println("Sent to RANGER: " + String(message));

  // Also show on matrix briefly
  strncpy(pendingBuf, message, sizeof(pendingBuf));
  newMessagePending = true;
}

// ────────────────────────────────────────────────────────────
String lookupMessage(const String &keyword) {
  String kw = keyword;
  kw.toLowerCase();
  for (int i = 0; i < DICT_SIZE; i++) {
    String dk = String(dictionary[i].keyword);
    dk.toLowerCase();
    if (dk == kw) return String(dictionary[i].message);
  }
  return "";
}

// ────────────────────────────────────────────────────────────
void loop() {
  // ── Matrix animation ─────────────────────────────────────
  if (matrix.displayAnimate()) {
    if (newMessagePending) {
      strncpy(displayBuf, pendingBuf, sizeof(displayBuf));
      newMessagePending = false;
    }
    matrix.displayText(displayBuf, PA_LEFT, 50, 500, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  }

  // ── Button 1 ─────────────────────────────────────────────
  bool btn1Now = digitalRead(BTN1_PIN);
  if (btn1Now == LOW && btn1Last == HIGH &&
      millis() - btn1Time > DEBOUNCE_MS) {
    btn1Time = millis();
    sendToRanger(BTN1_MSG);
  }
  btn1Last = btn1Now;

  // ── Button 2 ─────────────────────────────────────────────
  bool btn2Now = digitalRead(BTN2_PIN);
  if (btn2Now == LOW && btn2Last == HIGH &&
      millis() - btn2Time > DEBOUNCE_MS) {
    btn2Time = millis();
    sendToRanger(BTN2_MSG);
  }
  btn2Last = btn2Now;

  // ── Receive keyword from RANGER ──────────────────────────
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String raw = "";
    while (LoRa.available()) {
      raw += (char)LoRa.read();
    }

    if (raw.startsWith("KW:")) {
      String keyword = raw.substring(3);
      keyword.trim();
      Serial.println("Keyword from RANGER: " + keyword);

      String found = lookupMessage(keyword);
      if (found.length() > 0) {
        Serial.println("Matched: " + found);
        found.toCharArray(pendingBuf, sizeof(pendingBuf));
      } else {
        Serial.println("No match for: " + keyword);
        String notFound = "Unknown: " + keyword;
        notFound.toCharArray(pendingBuf, sizeof(pendingBuf));
      }
      newMessagePending = true;
      Serial.println("RSSI: " + String(LoRa.packetRssi()) + " dBm");
    }
  }
}