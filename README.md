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

## 📁 Repository Structure

```
RANGER-VILLAGE/
│
├── RANGER/
│   └── RANGER.ino              # RANGER station code
│
├── VILLAGE/
│   └── VILLAGE.ino             # VILLAGE station code
│
├── docs/
│   └── circuit.png             # Circuit diagram (add yours here)
│
└── README.md
```

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

---

## 🚀 Getting Started

### 1. Clone the repo

```bash
git clone https://github.com/dineshpec25-sys/RANGER-VILLAGE.git
cd RANGER-VILLAGE
```

### 2. Install libraries

In Arduino IDE, install all libraries listed above.

### 3. Upload RANGER code

- Open `RANGER/RANGER.ino`
- Select board: **ESP32 Dev Module**
- Select correct port (e.g. `/dev/ttyUSB0`)
- Click **Upload**
- Open Serial Monitor at **115200 baud**
- You should see:
```
================================
       RANGER STATION
================================
Keywords:
  [status] → What is your status?
  [water]  → Send water supply now!
  ...
```

### 4. Upload VILLAGE code

- Open `VILLAGE/VILLAGE.ino`
- Select board: **ESP32 Dev Module**
- Select correct port
- Click **Upload**
- Open Serial Monitor at **115200 baud**
- You should see:
```
================================
       VILLAGE STATION
================================
Matrix OK
LoRa Ready!
```

---

## 💬 Usage

### RANGER → VILLAGE

Type any keyword in RANGER's Serial Monitor and press **Enter**:

| Keyword | Message displayed on VILLAGE matrix |
|---|---|
| `status` | `What is your status?` |
| `water` | `Send water supply now!` |
| `food` | `Need food immediately!` |
| `safe` | `All clear. Stay safe.` |
| `report` | `Send your report now.` |
| `list` | _(prints all keywords in Serial Monitor)_ |

### VILLAGE → RANGER

Press a physical button on VILLAGE:

| Button | Message shown in RANGER Serial Monitor |
|---|---|
| Button 1 | `Village is SAFE.` |
| Button 2 | `EMERGENCY! Need help!` |

---

## ✏️ Customisation

### Add a new keyword (RANGER code)

```cpp
Message presets[] = {
  { "status", "What is your status?"  },
  { "mykey",  "My custom message."    },  // ← add here
};
int totalPresets = 6;  // ← update this number!
```

### Change button messages (VILLAGE code)

```cpp
String btn1Message = "Village is SAFE.";
String btn2Message = "EMERGENCY! Need help!";
```

### Adjust matrix scroll speed

```cpp
// Lower = faster | Range: 10–200
matrix.displayScroll(buf, PA_LEFT, PA_SCROLL_LEFT, 50);
```

### Adjust matrix brightness

```cpp
// Range: 0 (dim) to 15 (max bright)
matrix.setIntensity(5);
```

---

## 🔧 Troubleshooting

| Problem | Fix |
|---|---|
| `LoRa init FAILED!` | Recheck RST → GPIO 14 on **BOTTOM row** |
| `Matrix OK` but `LoRa FAILED` | Recheck all 8 SX1278 wires |
| LoRa init OK but no messages | Verify `SyncWord(0xF3)` on both ESP32s |
| Matrix shows garbage | Change `FC16_HW` to `GENERIC_HW` in code |
| Serial Monitor blank | Set baud rate to **115200** |
| Button not responding | Check GPIO 4 / GPIO 13 wired to GND |
| No packets received | Make sure antenna is attached to both modules |

---

## ⚠️ Important Notes

- **SX1278 VCC = 3.3V only** — 5V will permanently damage the module
- **MAX7219 VCC = 5V** — 3.3V will cause dim or no display
- **RST is on BOTTOM row at GPIO 14** — most common wiring mistake
- **Antenna must be attached** before powering on
- Both ESP32s must use **identical LoRa settings** to communicate

---

## 📄 License

This project is open source and available under the [MIT License](LICENSE).
