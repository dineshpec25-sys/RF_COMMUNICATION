# 📡 RANGER-VILLAGE — LoRa Dual Communication System

> Wireless text communication between two ESP32 stations using SX1278 LoRa at 433 MHz with an 8×32 LED matrix display.

---

## 🗺️ System Overview

```
┌─────────────────────────┐                        ┌─────────────────────────┐
│        RANGER           │                        │        VILLAGE          │
│    ESP32 WROOM-32       │◄──── LoRa 433 MHz ────►│    ESP32 WROOM-32D      │
│                         │                        │                         │
│  • Type keyword         │ ──── keyword msg ────► │  • Scrolls response     │
│    in Serial Monitor    │                        │    on 8×32 LED Matrix   │
│                         │                        │                         │
│  • Receives alert msg   │ ◄─── button press ──── │  • Press Button 1 or 2  │
│    in Serial Monitor    │                        │    to send alert        │
└─────────────────────────┘                        └─────────────────────────┘
```

---

## 🛠️ Hardware Used

| Component | Qty | Role |
|---|---|---|
| ESP32 WROOM-32 | 1 | RANGER station |
| ESP32 WROOM-32D | 1 | VILLAGE station |
| SX1278 LoRa Module | 2 | 433 MHz wireless link |
| MAX7219 8×32 LED Matrix | 1 | Message display at VILLAGE |
| Push Button | 2 | Alert triggers at VILLAGE |
| Jumper Wires | — | All connections |
| 433 MHz Antenna | 2 | One per LoRa module |

---

## 🔌 Circuit Diagram

### SX1278 LoRa → ESP32 (Both boards — identical wiring)

| SX1278 Pin | ESP32 GPIO | Board Label | Row |
|---|---|---|---|
| VCC | 3.3V | 3V3 | BOTTOM left |
| GND | GND | GND | TOP |
| SCK | GPIO 18 | 18 | TOP |
| MISO | GPIO 19 | 19 | TOP |
| MOSI | GPIO 23 | 23 | TOP |
| NSS | GPIO 5 | 5 | TOP |
| RST | GPIO 14 | 14 | **BOTTOM** ⚠️ |
| DIO0 | GPIO 2 | 2 | TOP |

### MAX7219 Matrix → ESP32 (VILLAGE only)

| MAX7219 Pin | ESP32 GPIO | Note |
|---|---|---|
| VCC | 5V | NOT 3.3V! |
| GND | GND | — |
| DIN | GPIO 23 | Shared MOSI |
| CS | GPIO 15 | Dedicated |
| CLK | GPIO 18 | Shared SCK |

### Push Buttons → ESP32 (VILLAGE only)

| Button | GPIO | Message Sent |
|---|---|---|
| Button 1 | GPIO 4 + GND | `Village is SAFE.` |
| Button 2 | GPIO 13 + GND | `EMERGENCY! Need help!` |

### Circuit Diagram

<!-- Add your circuit diagram image below -->
> 📷 _Circuit diagram coming soon — place your image at `docs/circuit.png` and uncomment the line below_
<!-- ![Circuit Diagram](docs/circuit.png) -->

---

## 📦 Libraries Required

Install via Arduino IDE → **Sketch → Manage Libraries**

| Library | Author | Install Name |
|---|---|---|
| LoRa | Sandeep Mistry | `LoRa` |
| MD_Parola | MajicDesigns | `MD_Parola` |
| MD_MAX72XX | MajicDesigns | `MD_MAX72XX` |
| SPI | Built-in | Pre-installed |

---

## ⚙️ LoRa Configuration

Both ESP32s **must** have identical settings:

```cpp
LoRa.begin(433E6);
LoRa.setSpreadingFactor(7);
LoRa.setSignalBandwidth(125E3);
LoRa.setCodingRate4(5);
LoRa.setSyncWord(0xF3);
LoRa.enableCrc();
```

