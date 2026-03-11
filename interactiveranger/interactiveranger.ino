/*
 * RANGER — Final Code
 * - Type keyword → sends to VILLAGE → shows on matrix
 * - Receives messages from VILLAGE → prints in Serial Monitor
 */

#include <SPI.h>
#include <LoRa.h>

#define LORA_SS   5
#define LORA_RST  14
#define LORA_DIO0 2

String inputBuffer = "";

void setup() {
  Serial.begin(115200);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(433E6)) {
    Serial.println("LoRa FAILED!");
    while (true);
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);

  Serial.println("=====================================");
  Serial.println("  RANGER READY");
  Serial.println("=====================================");
  Serial.println("Keywords: weather, water, fire, supply");
  Serial.println("          medical, safe, danger, food");
  Serial.println("Type a keyword + ENTER to send.");
  Serial.println("-------------------------------------");
}

void loop() {
  // ── Send: read Serial input ──────────────────────────────
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      inputBuffer.trim();
      if (inputBuffer.length() > 0) {
        String packet = "KW:" + inputBuffer;
        LoRa.beginPacket();
        LoRa.print(packet);
        LoRa.endPacket();
        Serial.println("▶ Sent keyword: \"" + inputBuffer + "\"");
        Serial.println("-------------------------------------");
        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
    }
  }

  // ── Receive: messages from VILLAGE ──────────────────────
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String raw = "";
    while (LoRa.available()) {
      raw += (char)LoRa.read();
    }
    if (raw.startsWith("MSG:")) {
      String msg = raw.substring(4);
      Serial.println();
      Serial.println("┌─ MESSAGE FROM VILLAGE ──────────────");
      Serial.println("│  " + msg);
      Serial.println("│  RSSI: " + String(LoRa.packetRssi()) + " dBm");
      Serial.println("└─────────────────────────────────────");
    }
  }
}