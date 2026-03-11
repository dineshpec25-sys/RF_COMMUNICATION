#include <SPI.h>
#include <LoRa.h>

#define LORA_SS   5
#define LORA_RST  14
#define LORA_DIO0 2

void setup() {
  Serial.begin(115200);
  Serial.println("Testing LoRa SX1278...");

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(433E6)) {
    Serial.println("FAIL — LoRa not found! Check wiring.");
    while (true);
  }

  Serial.println("OK — LoRa SX1278 detected!");
  Serial.println("Frequency: 433 MHz");
}

void loop() {}
// ```

// **Expected Serial Monitor output:**
// ```
// Testing LoRa SX1278...
// OK — LoRa SX1278 detected!
// Frequency: 433 MHz