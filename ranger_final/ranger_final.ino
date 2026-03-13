/*
 * RANGER_WIFI.ino
 * Two-way LoRa Communication System — RANGER Node
 *
 * Hardware: ESP32 WROOM-32E + SX1278 433MHz
 * LoRa Wiring: VCC→3.3V, GND→GND, SCK→GPIO18, MISO→GPIO19,
 *              MOSI→GPIO23, NSS→GPIO5,  RST→GPIO14, DIO0→GPIO2
 *
 * Libraries Required:
 *   - LoRa              by Sandeep Mistry  (v0.8.0+)
 *   - WebSockets        by Markus Sattler  (v2.4.0+)
 *   - ArduinoJson       by Benoit Blanchon (v6.x)
 *
 * RF Settings (must match VILLAGE node exactly):
 *   Frequency : 433 MHz
 *   SF        : 7
 *   BW        : 125 kHz
 *   CR        : 4/5
 *   CRC       : Enabled
 */

// ════════════════════════════════════════════════════════════════════════════
//  LIBRARIES
// ════════════════════════════════════════════════════════════════════════════
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <LoRa.h>

// ════════════════════════════════════════════════════════════════════════════
//  PIN DEFINITIONS
// ════════════════════════════════════════════════════════════════════════════
#define LORA_NSS   5
#define LORA_RST  14
#define LORA_DIO0  2

// ════════════════════════════════════════════════════════════════════════════
//  WiFi ACCESS POINT CONFIG
// ════════════════════════════════════════════════════════════════════════════
const char*      AP_SSID     = "RANGER_NET";
const char*      AP_PASSWORD = "ranger123";          // MUST be 8+ chars
const IPAddress  AP_IP      (192, 168, 4, 1);
const IPAddress  AP_GATEWAY (192, 168, 4, 1);
const IPAddress  AP_SUBNET  (255, 255, 255, 0);

// ════════════════════════════════════════════════════════════════════════════
//  SERVER INSTANCES
// ════════════════════════════════════════════════════════════════════════════
WebServer        httpServer(80);
WebSocketsServer webSocket(81);

// ════════════════════════════════════════════════════════════════════════════
//  MESSAGE HISTORY  (ring buffer — last 20 messages)
// ════════════════════════════════════════════════════════════════════════════
#define MAX_HISTORY 20
struct Message {
  String text;
  String direction;   // "FROM_VILLAGE" or "TO_VILLAGE"
  String timestamp;   // uptime-based HH:MM:SS
};
Message messageHistory[MAX_HISTORY];
int historyHead  = 0;
int historyCount = 0;

// ════════════════════════════════════════════════════════════════════════════
//  VALID KEYWORDS  (must match VILLAGE dictionary exactly)
// ════════════════════════════════════════════════════════════════════════════
const char* KEYWORDS[]  = {
  "weather","water","fire","supply","medical",
  "safe","danger","food","signal","base"
};
const int KEYWORD_COUNT = 10;

// ════════════════════════════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════════════════════════════
String uptimeString() {
  unsigned long s = millis() / 1000;
  unsigned long m = s / 60;  s %= 60;
  unsigned long h = m / 60;  m %= 60;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
  return String(buf);
}

void storeMessage(const String& text, const String& direction) {
  messageHistory[historyHead] = { text, direction, uptimeString() };
  historyHead = (historyHead + 1) % MAX_HISTORY;
  if (historyCount < MAX_HISTORY) historyCount++;
}

// Full history as JSON (sent to a newly connected WS client)
String buildStatusJson() {
  StaticJsonDocument<2048> doc;
  doc["type"]   = "status";
  doc["uptime"] = uptimeString();

  JsonArray hist  = doc.createNestedArray("history");
  int start = (historyCount < MAX_HISTORY) ? 0 : historyHead;
  for (int i = 0; i < historyCount; i++) {
    int idx = (start + i) % MAX_HISTORY;
    JsonObject entry  = hist.createNestedObject();
    entry["text"]      = messageHistory[idx].text;
    entry["direction"] = messageHistory[idx].direction;
    entry["time"]      = messageHistory[idx].timestamp;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

// Broadcast one new message to every connected WS client
void broadcastMessage(const String& text, const String& direction) {
  storeMessage(text, direction);

  StaticJsonDocument<256> doc;
  doc["type"]      = "message";
  doc["text"]      = text;
  doc["direction"] = direction;
  doc["time"]      = uptimeString();

  String payload;
  serializeJson(doc, payload);
  webSocket.broadcastTXT(payload);
  Serial.printf("[WS]   Broadcast → %s\n", payload.c_str());
}

// ════════════════════════════════════════════════════════════════════════════
//  WEBSOCKET EVENT HANDLER
// ════════════════════════════════════════════════════════════════════════════
void onWebSocketEvent(uint8_t clientNum, WStype_t type,
                      uint8_t* payload, size_t length) {
  switch (type) {

    // ── Client connected ──────────────────────────────────────────────────
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(clientNum);
      Serial.printf("[WS]   Client #%u connected from %s\n",
                    clientNum, ip.toString().c_str());
      // Send full message history to the new client
      String statusPayload = buildStatusJson();
      webSocket.sendTXT(clientNum, statusPayload);
      break;
    }

    // ── Client disconnected ───────────────────────────────────────────────
    case WStype_DISCONNECTED:
      Serial.printf("[WS]   Client #%u disconnected\n", clientNum);
      break;

    // ── Text message from dashboard ───────────────────────────────────────
    case WStype_TEXT: {
      String msg = String((char*)payload);
      Serial.printf("[WS]   From client #%u: %s\n", clientNum, msg.c_str());

      StaticJsonDocument<128> doc;
      DeserializationError err = deserializeJson(doc, msg);
      if (err) {
        Serial.printf("[WS]   JSON parse error: %s\n", err.c_str());
        break;
      }

      if (doc.containsKey("keyword")) {
        String kw = doc["keyword"].as<String>();
        kw.toLowerCase();
        kw.trim();

        // Validate keyword
        bool valid = false;
        for (int i = 0; i < KEYWORD_COUNT; i++) {
          if (kw == KEYWORDS[i]) { valid = true; break; }
        }

        if (valid) {
          // Transmit via LoRa
          String pkt = "KW:" + kw;
          LoRa.beginPacket();
          LoRa.print(pkt);
          LoRa.endPacket();
          Serial.printf("[LORA] Sent → %s\n", pkt.c_str());
          broadcastMessage(kw, "TO_VILLAGE");

          // ACK to sender
          StaticJsonDocument<64> ack;
          ack["type"]    = "ack";
          ack["keyword"] = kw;
          String ackStr;
          serializeJson(ack, ackStr);
          webSocket.sendTXT(clientNum, ackStr);

        } else {
          Serial.printf("[WS]   Invalid keyword rejected: %s\n", kw.c_str());
          StaticJsonDocument<64> errDoc;
          errDoc["type"] = "error";
          errDoc["msg"]  = "Invalid keyword: " + kw;
          String errStr;
          serializeJson(errDoc, errStr);
          webSocket.sendTXT(clientNum, errStr);
        }
      }
      break;
    }

    default: break;
  }
}

// ════════════════════════════════════════════════════════════════════════════
//  HTML DASHBOARD — split into 2 PROGMEM chunks (avoids 32 KB limit)
// ════════════════════════════════════════════════════════════════════════════
static const char HTML_PART1[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RANGER Dashboard</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',sans-serif;background:#0d1117;color:#e6edf3;min-height:100vh}

/* ── HEADER ── */
header{background:#161b22;border-bottom:1px solid #30363d;padding:12px 20px;
       display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:8px}
header h1{font-size:1.15rem;color:#58a6ff;letter-spacing:1px}
#ws-status{font-size:.75rem;padding:4px 12px;border-radius:12px;background:#21262d;
           border:1px solid #30363d;transition:all .3s}
#ws-status.online{color:#3fb950;border-color:#3fb950}
#ws-status.offline{color:#f85149;border-color:#f85149}

/* ── MAIN LAYOUT ── */
.main-layout{display:grid;grid-template-columns:1fr 340px;gap:0;height:calc(100vh - 52px)}
@media(max-width:860px){.main-layout{grid-template-columns:1fr;height:auto}}

/* ── MAP AREA ── */
.map-area{position:relative;background:#0d1117;overflow:hidden;border-right:1px solid #30363d}
.map-wrap{position:relative;width:100%;height:100%;min-height:420px}

/* SVG map */
#terrain-map{width:100%;height:100%;display:block}

/* Sector polygons */
.sector{cursor:pointer;transition:opacity .2s, filter .2s}
.sector:hover{opacity:.85;filter:brightness(1.25)}
.sector.active{filter:brightness(1.5) drop-shadow(0 0 8px #58a6ff)}

/* Sector labels */
.sec-label{font-size:11px;fill:#e6edf3;font-weight:600;pointer-events:none;
           text-shadow:0 1px 3px #000;font-family:'Segoe UI',sans-serif}

/* Village marker */
.village-marker{cursor:pointer;animation:pulse 2s infinite}
@keyframes pulse{
  0%,100%{r:7;opacity:1}
  50%{r:10;opacity:.6}
}
.village-label{font-size:10px;fill:#fff;font-weight:700;pointer-events:none;
               font-family:'Segoe UI',sans-serif}

/* Ranger marker */
.ranger-dot{fill:#f78166;filter:drop-shadow(0 0 6px #f78166)}

/* Map legend */
.map-legend{position:absolute;bottom:12px;left:12px;background:#161b22cc;
            border:1px solid #30363d;border-radius:8px;padding:8px 12px;font-size:.72rem}
.legend-row{display:flex;align-items:center;gap:6px;margin-bottom:4px}
.legend-row:last-child{margin-bottom:0}
.leg-dot{width:10px;height:10px;border-radius:50%;flex-shrink:0}

/* Tooltip */
#map-tooltip{position:absolute;background:#161b22;border:1px solid #58a6ff;
             border-radius:6px;padding:6px 10px;font-size:.75rem;color:#e6edf3;
             pointer-events:none;display:none;z-index:10;white-space:nowrap}

/* ── RIGHT PANEL ── */
.right-panel{display:flex;flex-direction:column;overflow:hidden;background:#0d1117}

/* Tab bar */
.tab-bar{display:flex;background:#161b22;border-bottom:1px solid #30363d;flex-shrink:0}
.tab-btn{flex:1;padding:10px 4px;font-size:.78rem;color:#8b949e;background:none;
         border:none;cursor:pointer;border-bottom:2px solid transparent;transition:all .2s}
.tab-btn:hover{color:#e6edf3}
.tab-btn.active{color:#58a6ff;border-bottom-color:#58a6ff}

/* Tab content */
.tab-content{flex:1;overflow-y:auto;padding:14px}
.tab-pane{display:none}
.tab-pane.active{display:block}

/* ── VILLAGE INFO PANEL ── */
.village-info-header{display:flex;align-items:center;gap:10px;margin-bottom:14px;
                      padding-bottom:12px;border-bottom:1px solid #30363d}
.village-info-header .vi-icon{font-size:2rem;line-height:1}
.village-info-header h3{font-size:1rem;color:#e6edf3;margin-bottom:2px}
.village-info-header p{font-size:.75rem;color:#8b949e}
.vi-badge{display:inline-block;font-size:.65rem;padding:2px 8px;border-radius:10px;
          font-weight:600;margin-top:4px}
.vi-badge.online{background:#238636;color:#fff}
.vi-badge.offline{background:#6e4040;color:#ffa198}

/* Sensor cards */
.sensor-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:14px}
.sensor-card{background:#161b22;border:1px solid #30363d;border-radius:8px;
             padding:12px;text-align:center;position:relative;overflow:hidden}
.sensor-card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px}
.sensor-card.temp::before{background:linear-gradient(90deg,#ff6b35,#f7c59f)}
.sensor-card.hum::before{background:linear-gradient(90deg,#58a6ff,#79c0ff)}
.sensor-card.bat::before{background:linear-gradient(90deg,#3fb950,#7ee787)}
.sensor-card.sig::before{background:linear-gradient(90deg,#d2a8ff,#bc8cff)}
.sensor-icon{font-size:1.4rem;margin-bottom:4px}
.sensor-val{font-size:1.5rem;font-weight:700;color:#e6edf3;line-height:1.1}
.sensor-unit{font-size:.7rem;color:#8b949e}
.sensor-label{font-size:.7rem;color:#8b949e;margin-top:2px;text-transform:uppercase;
              letter-spacing:.05em}

/* Temp gauge bar */
.temp-bar-wrap{margin:4px 0 14px;background:#21262d;border-radius:4px;height:8px;overflow:hidden}
.temp-bar{height:100%;border-radius:4px;transition:width .8s ease;
          background:linear-gradient(90deg,#1f6feb,#58a6ff,#f78166,#ff6b35)}

/* Status rows */
.info-row{display:flex;justify-content:space-between;align-items:center;
          padding:7px 0;border-bottom:1px solid #21262d;font-size:.82rem}
.info-row:last-child{border-bottom:none}
.info-key{color:#8b949e}
.info-val{color:#e6edf3;font-weight:500}

/* No selection state */
.no-selection{display:flex;flex-direction:column;align-items:center;justify-content:center;
              height:200px;color:#8b949e;text-align:center;gap:8px}
.no-selection .ns-icon{font-size:2.5rem;opacity:.4}

/* Alert row */
.alert-row{display:flex;align-items:center;gap:8px;padding:8px 10px;border-radius:6px;
           font-size:.8rem;margin-bottom:8px}
.alert-row.danger{background:#2d1a1a;border:1px solid #f85149;color:#ffa198}
.alert-row.safe{background:#0d1f12;border:1px solid #3fb950;color:#7ee787}
.alert-row.warn{background:#2a1f0a;border:1px solid #d29922;color:#e3b341}

/* ── KEYWORD + LOG (reused from before) ── */
.kw-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
button.kw{width:100%;padding:9px 4px;border:1px solid #30363d;border-radius:6px;
          background:#21262d;color:#e6edf3;cursor:pointer;font-size:.82rem;
          transition:background .15s,border-color .15s,color .15s}
button.kw:hover{background:#388bfd22;border-color:#58a6ff;color:#58a6ff}
button.kw.danger-kw:hover{background:#f8514922;border-color:#f85149;color:#f85149}
button.kw.active{background:#1f6feb !important;border-color:#388bfd !important;color:#fff !important}
button.kw:disabled{opacity:.4;cursor:not-allowed}

/* log */
#log{list-style:none;max-height:260px;overflow-y:auto;display:flex;flex-direction:column;gap:5px}
#log::-webkit-scrollbar{width:3px}
#log::-webkit-scrollbar-thumb{background:#30363d}
#log li{padding:6px 9px;border-radius:5px;font-size:.78rem;border-left:3px solid transparent;line-height:1.4}
#log li.from{background:#0d1f12;border-color:#3fb950;color:#7ee787}
#log li.to{background:#0d1926;border-color:#388bfd;color:#79c0ff}
#log li.sys{background:#1a1a1a;border-color:#8b949e;color:#8b949e;font-style:italic}
.badge{display:inline-block;font-size:.6rem;padding:1px 6px;border-radius:10px;
       margin-right:5px;font-weight:700;vertical-align:middle}
.badge.from{background:#238636;color:#fff}
.badge.to{background:#1f6feb;color:#fff}
.badge.sys{background:#444;color:#ccc}
.sec-title{font-size:.75rem;color:#8b949e;text-transform:uppercase;
           letter-spacing:.08em;margin-bottom:10px}
</style>
</head>
<body>
<header>
  <h1>&#128225; RANGER Command Dashboard</h1>
  <span id="ws-status" class="offline">&#9679; Disconnected</span>
</header>

<div class="main-layout">

  <!-- ══════════════ MAP ══════════════ -->
  <div class="map-area">
    <div class="map-wrap">

      <svg id="terrain-map" viewBox="0 0 700 500" preserveAspectRatio="xMidYMid meet"
           xmlns="http://www.w3.org/2000/svg">

        <!-- ── Background terrain ── -->
        <defs>
          <linearGradient id="bgGrad" x1="0" y1="0" x2="1" y2="1">
            <stop offset="0%"   stop-color="#0a1628"/>
            <stop offset="100%" stop-color="#0d1f12"/>
          </linearGradient>
          <filter id="glow">
            <feGaussianBlur stdDeviation="2.5" result="blur"/>
            <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
          </filter>
          <!-- terrain texture lines -->
          <pattern id="grid" width="40" height="40" patternUnits="userSpaceOnUse">
            <path d="M 40 0 L 0 0 0 40" fill="none" stroke="#ffffff08" stroke-width="0.5"/>
          </pattern>
        </defs>

        <!-- Base fill -->
        <rect width="700" height="500" fill="url(#bgGrad)"/>
        <rect width="700" height="500" fill="url(#grid)"/>

        <!-- ── River ── -->
        <path d="M 0 310 Q 80 290 140 320 Q 200 350 260 330 Q 320 310 380 340 Q 440 370 500 350 Q 580 325 700 360"
              fill="none" stroke="#1a4a7a" stroke-width="12" opacity=".7"/>
        <path d="M 0 310 Q 80 290 140 320 Q 200 350 260 330 Q 320 310 380 340 Q 440 370 500 350 Q 580 325 700 360"
              fill="none" stroke="#215d9a" stroke-width="5" opacity=".5"/>

        <!-- ── Forest patches ── -->
        <ellipse cx="120" cy="150" rx="60" ry="40" fill="#143320" opacity=".7"/>
        <ellipse cx="560" cy="200" rx="50" ry="35" fill="#143320" opacity=".6"/>
        <ellipse cx="400" cy="430" rx="70" ry="30" fill="#143320" opacity=".5"/>

        <!-- Forest dots -->
        <circle cx="100" cy="140" r="5" fill="#1a4d2e" opacity=".8"/>
        <circle cx="120" cy="135" r="6" fill="#1a4d2e" opacity=".8"/>
        <circle cx="140" cy="143" r="5" fill="#1a4d2e" opacity=".8"/>
        <circle cx="110" cy="158" r="5" fill="#1a4d2e" opacity=".7"/>
        <circle cx="130" cy="162" r="6" fill="#1a4d2e" opacity=".7"/>
        <circle cx="545" cy="192" r="5" fill="#1a4d2e" opacity=".7"/>
        <circle cx="565" cy="188" r="6" fill="#1a4d2e" opacity=".7"/>
        <circle cx="575" cy="200" r="5" fill="#1a4d2e" opacity=".7"/>

        <!-- ── Hills ── -->
        <ellipse cx="300" cy="80" rx="90" ry="50" fill="#16263a" opacity=".5"/>
        <ellipse cx="620" cy="420" rx="70" ry="40" fill="#16263a" opacity=".4"/>

        <!-- ══════ SECTORS ══════ -->

        <!-- SECTOR A — top-left -->
        <polygon id="sec-A"
          class="sector" data-sector="A"
          points="20,20 280,20 260,180 160,220 20,200"
          fill="#1a3a2a" stroke="#3fb95066" stroke-width="1.5"
          onclick="selectSector('A')" />
        <text class="sec-label" x="120" y="95" text-anchor="middle">SECTOR A</text>
        <text class="sec-label" x="120" y="109" text-anchor="middle" style="font-size:9px;fill:#8b949e">North Forest</text>

        <!-- SECTOR B — top-right -->
        <polygon id="sec-B"
          class="sector" data-sector="B"
          points="300,20 680,20 680,200 520,230 290,190"
          fill="#1a2a3a" stroke="#388bfd66" stroke-width="1.5"
          onclick="selectSector('B')" />
        <text class="sec-label" x="490" y="95" text-anchor="middle">SECTOR B</text>
        <text class="sec-label" x="490" y="109" text-anchor="middle" style="font-size:9px;fill:#8b949e">East Ridge</text>

        <!-- SECTOR C — middle-left  ← HAS VILLAGE ESP -->
        <polygon id="sec-C"
          class="sector" data-sector="C"
          points="20,220 160,230 250,290 230,390 20,380"
          fill="#2a1a3a" stroke="#bc8cff66" stroke-width="1.5"
          onclick="selectSector('C')" />
        <text class="sec-label" x="105" y="295" text-anchor="middle">SECTOR C</text>
        <text class="sec-label" x="105" y="309" text-anchor="middle" style="font-size:9px;fill:#bc8cff">▲ Village Node</text>

        <!-- SECTOR D — centre -->
        <polygon id="sec-D"
          class="sector" data-sector="D"
          points="270,195 520,235 540,360 380,390 240,370 240,300"
          fill="#1a2618" stroke="#3fb95055" stroke-width="1.5"
          onclick="selectSector('D')" />
        <text class="sec-label" x="385" y="290" text-anchor="middle">SECTOR D</text>
        <text class="sec-label" x="385" y="304" text-anchor="middle" style="font-size:9px;fill:#8b949e">Central Valley</text>

        <!-- SECTOR E — bottom-right -->
        <polygon id="sec-E"
          class="sector" data-sector="E"
          points="540,250 680,215 680,480 400,480 390,400 540,370"
          fill="#2a1e10" stroke="#d2990055" stroke-width="1.5"
          onclick="selectSector('E')" />
        <text class="sec-label" x="570" y="360" text-anchor="middle">SECTOR E</text>
        <text class="sec-label" x="570" y="374" text-anchor="middle" style="font-size:9px;fill:#8b949e">South Outpost</text>

        <!-- SECTOR F — bottom-left -->
        <polygon id="sec-F"
          class="sector" data-sector="F"
          points="20,395 230,395 370,480 20,480"
          fill="#1a1a2a" stroke="#58a6ff44" stroke-width="1.5"
          onclick="selectSector('F')" />
        <text class="sec-label" x="155" y="448" text-anchor="middle">SECTOR F</text>
        <text class="sec-label" x="155" y="462" text-anchor="middle" style="font-size:9px;fill:#8b949e">Lowlands</text>

        <!-- ══════ VILLAGE NODE (Sector C) ══════ -->
        <circle class="village-marker" cx="130" cy="340" r="7"
                fill="#bc8cff" stroke="#fff" stroke-width="1.5"
                onclick="selectSector('C')" filter="url(#glow)"/>
        <rect x="90" y="352" width="80" height="18" rx="3"
              fill="#21262dcc" stroke="#bc8cff44" stroke-width="1"/>
        <text class="village-label" x="130" y="365" text-anchor="middle">Village ESP32</text>

        <!-- ══════ RANGER BASE ══════ -->
        <rect x="316" y="234" width="16" height="16" rx="2"
              fill="#f78166" class="ranger-dot" stroke="#fff" stroke-width="1"/>
        <rect x="290" y="255" width="68" height="16" rx="3"
              fill="#21262dcc" stroke="#f7816644" stroke-width="1"/>
        <text x="324" y="267" text-anchor="middle"
              style="font-size:9px;fill:#f78166;font-weight:700;font-family:'Segoe UI',sans-serif">
          ◉ RANGER BASE
        </text>

        <!-- ══════ DISTANCE MARKERS ══════ -->
        <line x1="130" y1="334" x2="320" y2="245" stroke="#ffffff20" stroke-width="1" stroke-dasharray="4,4"/>
        <text x="218" y="282" text-anchor="middle"
              style="font-size:8px;fill:#ffffff40;font-family:'Segoe UI',sans-serif">~2.4 km</text>

        <!-- ══════ SECTOR BOUNDARIES (top overlay) ══════ -->
        <line x1="280" y1="20" x2="260" y2="180" stroke="#ffffff15" stroke-width="1"/>
        <line x1="300" y1="20" x2="290" y2="190" stroke="#ffffff15" stroke-width="1"/>

        <!-- Compass -->
        <g transform="translate(650,50)">
          <circle cx="0" cy="0" r="20" fill="#161b22" stroke="#30363d" stroke-width="1"/>
          <text x="0" y="-8"  text-anchor="middle" style="font-size:8px;fill:#e6edf3;font-weight:700;font-family:sans-serif">N</text>
          <text x="0" y="13"  text-anchor="middle" style="font-size:7px;fill:#8b949e;font-family:sans-serif">S</text>
          <text x="-12" y="3" text-anchor="middle" style="font-size:7px;fill:#8b949e;font-family:sans-serif">W</text>
          <text x="12"  y="3" text-anchor="middle" style="font-size:7px;fill:#8b949e;font-family:sans-serif">E</text>
          <line x1="0" y1="-16" x2="0" y2="0"  stroke="#f78166" stroke-width="1.5"/>
          <line x1="0" y1="0"   x2="0" y2="15"  stroke="#8b949e" stroke-width="1.5"/>
        </g>

        <!-- Scale bar -->
        <g transform="translate(20,470)">
          <line x1="0" y1="0" x2="80" y2="0" stroke="#8b949e" stroke-width="1"/>
          <line x1="0" y1="-4" x2="0" y2="4"  stroke="#8b949e" stroke-width="1"/>
          <line x1="80" y1="-4" x2="80" y2="4" stroke="#8b949e" stroke-width="1"/>
          <text x="40" y="-6" text-anchor="middle" style="font-size:8px;fill:#8b949e;font-family:sans-serif">5 km</text>
        </g>
      </svg>

      <!-- Tooltip -->
      <div id="map-tooltip"></div>

      <!-- Legend -->
      <div class="map-legend">
        <div class="legend-row"><div class="leg-dot" style="background:#bc8cff"></div>Village ESP32</div>
        <div class="legend-row"><div class="leg-dot" style="background:#f78166"></div>Ranger Base</div>
        <div class="legend-row"><div class="leg-dot" style="background:#215d9a;border-radius:2px"></div>River</div>
        <div class="legend-row"><div class="leg-dot" style="background:#1a4d2e"></div>Forest</div>
      </div>
    </div>
  </div>

  <!-- ══════════════ RIGHT PANEL ══════════════ -->
  <div class="right-panel">
    <div class="tab-bar">
      <button class="tab-btn active" onclick="switchTab('village')">&#127981; Village Info</button>
      <button class="tab-btn"        onclick="switchTab('control')">&#128228; Control</button>
      <button class="tab-btn"        onclick="switchTab('log')">&#128203; Log</button>
    </div>

    <!-- ── TAB: VILLAGE INFO ── -->
    <div class="tab-content">
      <div id="tab-village" class="tab-pane active">

        <!-- No sector selected yet -->
        <div id="no-selection">
          <div class="no-selection">
            <div class="ns-icon">&#128205;</div>
            <div><strong>No sector selected</strong></div>
            <div style="font-size:.78rem">Click a sector on the map<br>to view node information</div>
          </div>
        </div>

        <!-- Village info (shown when Sector C clicked) -->
        <div id="village-panel" style="display:none">

          <div class="village-info-header">
            <div class="vi-icon">&#127981;</div>
            <div>
              <h3 id="vi-name">Village Alpha</h3>
              <p id="vi-sector">Sector C — North Forest</p>
              <span id="vi-status-badge" class="vi-badge online">&#9679; ONLINE</span>
            </div>
          </div>

          <!-- Alert -->
          <div id="vi-alert" class="alert-row safe">
            <span>&#9989;</span>
            <span id="vi-alert-text">All clear — no active alerts</span>
          </div>

          <!-- Temperature highlight -->
          <div class="sensor-card temp" style="margin-bottom:10px;padding:14px">
            <div style="display:flex;justify-content:space-between;align-items:flex-start">
              <div>
                <div class="sensor-icon">&#127777;</div>
                <div class="sensor-val"><span id="vi-temp">28.4</span><span class="sensor-unit"> °C</span></div>
                <div class="sensor-label">Temperature</div>
              </div>
              <div style="text-align:right">
                <div style="font-size:.7rem;color:#8b949e">Feels like</div>
                <div style="font-size:1rem;color:#f7c59f;font-weight:600" id="vi-feels">31°C</div>
                <div style="font-size:.65rem;color:#8b949e;margin-top:4px" id="vi-temp-status">Warm</div>
              </div>
            </div>
            <div style="margin-top:10px">
              <div style="display:flex;justify-content:space-between;font-size:.7rem;color:#8b949e;margin-bottom:3px">
                <span>10°C</span><span id="vi-temp-label">28.4°C</span><span>45°C</span>
              </div>
              <div class="temp-bar-wrap">
                <div class="temp-bar" id="vi-temp-bar" style="width:52%"></div>
              </div>
            </div>
          </div>

          <!-- Other sensors -->
          <div class="sensor-grid">
            <div class="sensor-card hum">
              <div class="sensor-icon">&#128167;</div>
              <div class="sensor-val"><span id="vi-hum">64</span><span class="sensor-unit">%</span></div>
              <div class="sensor-label">Humidity</div>
            </div>
            <div class="sensor-card bat">
              <div class="sensor-icon">&#128267;</div>
              <div class="sensor-val"><span id="vi-bat">87</span><span class="sensor-unit">%</span></div>
              <div class="sensor-label">Battery</div>
            </div>
            <div class="sensor-card sig">
              <div class="sensor-icon">&#128246;</div>
              <div class="sensor-val"><span id="vi-rssi">-72</span><span class="sensor-unit">dBm</span></div>
              <div class="sensor-label">LoRa RSSI</div>
            </div>
            <div class="sensor-card" style="--c:#e3b341">
              <div class="sensor-icon">&#128337;</div>
              <div class="sensor-val" style="font-size:1.1rem"><span id="vi-last-seen">0s</span></div>
              <div class="sensor-label">Last Seen</div>
            </div>
          </div>

          <!-- Info rows -->
          <div class="sec-title">Node Details</div>
          <div class="info-row"><span class="info-key">Node ID</span><span class="info-val">VILL-01</span></div>
          <div class="info-row"><span class="info-key">Firmware</span><span class="info-val">v1.2.0</span></div>
          <div class="info-row"><span class="info-key">LoRa Freq</span><span class="info-val">433 MHz</span></div>
          <div class="info-row"><span class="info-key">SF / BW</span><span class="info-val">SF7 / 125 kHz</span></div>
          <div class="info-row"><span class="info-key">Packets RX</span><span class="info-val" id="vi-pkt-rx">0</span></div>
          <div class="info-row"><span class="info-key">Packets TX</span><span class="info-val" id="vi-pkt-tx">0</span></div>
          <div class="info-row"><span class="info-key">Uptime</span><span class="info-val" id="vi-uptime">--:--:--</span></div>

        </div>

        <!-- Generic sector info (non-C sectors) -->
        <div id="generic-sector-panel" style="display:none">
          <div class="village-info-header">
            <div class="vi-icon">&#128205;</div>
            <div>
              <h3 id="gs-name">Sector X</h3>
              <p id="gs-desc">No node deployed</p>
              <span class="vi-badge offline">&#9679; NO NODE</span>
            </div>
          </div>
          <div class="alert-row warn">
            <span>&#9888;</span>
            <span>No ESP32 node is deployed in this sector</span>
          </div>
          <div class="info-row"><span class="info-key">Status</span><span class="info-val">Unmonitored</span></div>
          <div class="info-row"><span class="info-key">Coverage</span><span class="info-val" id="gs-coverage">—</span></div>
          <div class="info-row"><span class="info-key">Terrain</span><span class="info-val" id="gs-terrain">—</span></div>
          <div class="info-row"><span class="info-key">Node</span><span class="info-val">Not assigned</span></div>
        </div>

      </div><!-- end tab-village -->

      <!-- ── TAB: CONTROL ── -->
      <div id="tab-control" class="tab-pane">
        <div class="sec-title">Send Keyword to Village</div>
        <div class="kw-grid">
          <button class="kw" onclick="sendKW('weather')">&#127780; Weather</button>
          <button class="kw" onclick="sendKW('water')">&#128167; Water</button>
          <button class="kw danger-kw" onclick="sendKW('fire')">&#128293; Fire</button>
          <button class="kw" onclick="sendKW('supply')">&#128230; Supply</button>
          <button class="kw" onclick="sendKW('medical')">&#10133; Medical</button>
          <button class="kw" onclick="sendKW('safe')">&#9989; Safe</button>
          <button class="kw danger-kw" onclick="sendKW('danger')">&#9888; Danger</button>
          <button class="kw" onclick="sendKW('food')">&#127858; Food</button>
          <button class="kw" onclick="sendKW('signal')">&#128246; Signal</button>
          <button class="kw" onclick="sendKW('base')">&#127963; Base</button>
        </div>
        <div style="margin-top:14px">
          <div class="sec-title">System Status</div>
          <div class="info-row"><span class="info-key">Uptime</span><span class="info-val" id="uptime-val">--:--:--</span></div>
          <div class="info-row"><span class="info-key">WS Clients</span><span class="info-val" id="conn-count">—</span></div>
          <div class="info-row"><span class="info-key">Packets RX</span><span class="info-val" id="rx-count">0</span></div>
          <div class="info-row"><span class="info-key">Packets TX</span><span class="info-val" id="tx-count">0</span></div>
        </div>
        <div style="margin-top:14px">
          <div class="sec-title">Last Village Message</div>
          <div id="last-msg" style="background:#0d1117;border:1px solid #30363d;border-radius:6px;
               padding:8px 10px;font-size:.84rem;min-height:2.5em;color:#e6edf3;line-height:1.5">
            Waiting for Village...
          </div>
        </div>
      </div>

      <!-- ── TAB: LOG ── -->
      <div id="tab-log" class="tab-pane">
        <div class="sec-title">Message Log</div>
        <ul id="log">
          <li class="sys"><span class="badge sys">SYS</span>Connecting to RANGER...</li>
        </ul>
      </div>

    </div><!-- end tab-content -->
  </div><!-- end right-panel -->

</div><!-- end main-layout -->
)rawhtml";

static const char HTML_PART2[] PROGMEM = R"rawhtml(<script>
// ══════════════════════════════════════════════
//  SECTOR DATA
// ══════════════════════════════════════════════
const SECTORS = {
  A: { name:'Sector A',  desc:'North Forest Zone',   terrain:'Dense forest',  coverage:'8.2 km²', hasNode:false },
  B: { name:'Sector B',  desc:'East Ridge Zone',     terrain:'Rocky highland',coverage:'11.4 km²',hasNode:false },
  C: { name:'Sector C',  desc:'Central West Zone',   terrain:'Mixed terrain', coverage:'7.6 km²', hasNode:true  },
  D: { name:'Sector D',  desc:'Central Valley Zone', terrain:'Open valley',   coverage:'13.1 km²',hasNode:false },
  E: { name:'Sector E',  desc:'South Outpost Zone',  terrain:'Dry lowlands',  coverage:'9.8 km²', hasNode:false },
  F: { name:'Sector F',  desc:'Southern Lowlands',   terrain:'Flat plains',   coverage:'6.3 km²', hasNode:false }
};

// ══════════════════════════════════════════════
//  DUMMY VILLAGE DATA  (simulated live updates)
// ══════════════════════════════════════════════
const village = {
  temp:     28.4,
  humidity: 64,
  battery:  87,
  rssi:     -72,
  pktRx:    0,
  pktTx:    0,
  bootTime: Date.now(),
  lastSeen: 0,   // seconds ago
  alert:    'safe'
};

// ── Simulate gentle sensor drift ──
setInterval(() => {
  village.temp     = +(village.temp     + (Math.random()-.5)*.4).toFixed(1);
  village.humidity = Math.round(Math.min(99, Math.max(10, village.humidity + (Math.random()-.5)*1.5)));
  village.battery  = Math.max(0, village.battery - .005);
  village.rssi     = Math.round(village.rssi + (Math.random()-.5)*2);
  village.lastSeen = Math.floor((Date.now() - village.bootTime)/1000) % 30;
  // Occasionally flip alert for demo
  if(Math.random() < .003) village.alert = village.alert==='safe'?'danger':'safe';
  refreshVillagePanel();
}, 2000);

// ══════════════════════════════════════════════
//  MAP INTERACTION
// ══════════════════════════════════════════════
let activeSector = null;

function selectSector(id) {
  // Deactivate previous
  if(activeSector) {
    const prev = document.getElementById('sec-'+activeSector);
    if(prev) prev.classList.remove('active');
  }
  activeSector = id;
  const el = document.getElementById('sec-'+id);
  if(el) el.classList.add('active');

  const sec = SECTORS[id];
  if(!sec) return;

  // Switch to village tab
  switchTab('village');

  if(sec.hasNode) {
    // Show village panel
    document.getElementById('no-selection').style.display       = 'none';
    document.getElementById('generic-sector-panel').style.display = 'none';
    document.getElementById('village-panel').style.display      = 'block';
    document.getElementById('vi-name').textContent   = 'Village Alpha';
    document.getElementById('vi-sector').textContent = sec.name + ' — ' + sec.desc;
    refreshVillagePanel();
  } else {
    // Show generic panel
    document.getElementById('no-selection').style.display       = 'none';
    document.getElementById('village-panel').style.display      = 'none';
    document.getElementById('generic-sector-panel').style.display = 'block';
    document.getElementById('gs-name').textContent     = sec.name;
    document.getElementById('gs-desc').textContent     = sec.desc;
    document.getElementById('gs-coverage').textContent = sec.coverage;
    document.getElementById('gs-terrain').textContent  = sec.terrain;
  }
}

// Hover tooltip
document.querySelectorAll('.sector').forEach(el => {
  const id  = el.dataset.sector;
  const sec = SECTORS[id];
  el.addEventListener('mousemove', e => {
    const tip   = document.getElementById('map-tooltip');
    const rect  = el.closest('.map-area').getBoundingClientRect();
    tip.style.display = 'block';
    tip.style.left    = (e.clientX - rect.left + 12) + 'px';
    tip.style.top     = (e.clientY - rect.top  + 12) + 'px';
    tip.innerHTML = `<strong>${sec.name}</strong> &nbsp;${sec.hasNode
      ? '<span style="color:#bc8cff">▲ Village Node</span>'
      : '<span style="color:#8b949e">No node</span>'}`;
  });
  el.addEventListener('mouseleave', () => {
    document.getElementById('map-tooltip').style.display = 'none';
  });
});

// ══════════════════════════════════════════════
//  REFRESH VILLAGE PANEL
// ══════════════════════════════════════════════
function refreshVillagePanel() {
  if(document.getElementById('village-panel').style.display === 'none') return;

  // Temperature
  const t   = village.temp;
  const pct = Math.min(100, Math.max(0, ((t-10)/(45-10))*100));
  document.getElementById('vi-temp').textContent       = t.toFixed(1);
  document.getElementById('vi-feels').textContent      = (t+2.6).toFixed(0)+'°C';
  document.getElementById('vi-temp-bar').style.width   = pct+'%';
  document.getElementById('vi-temp-label').textContent = t.toFixed(1)+'°C';
  document.getElementById('vi-temp-status').textContent =
    t < 15 ? 'Cold' : t < 25 ? 'Comfortable' : t < 35 ? 'Warm' : 'Hot';

  // Other sensors
  document.getElementById('vi-hum').textContent      = village.humidity;
  document.getElementById('vi-bat').textContent      = Math.round(village.battery);
  document.getElementById('vi-rssi').textContent     = village.rssi;
  document.getElementById('vi-last-seen').textContent = village.lastSeen + 's';

  // Uptime
  const s = Math.floor((Date.now()-village.bootTime)/1000);
  document.getElementById('vi-uptime').textContent = fmtTime(s);

  // Packets (mirror global counters)
  document.getElementById('vi-pkt-rx').textContent = rxCount;
  document.getElementById('vi-pkt-tx').textContent = txCount;

  // Alert
  const alertEl   = document.getElementById('vi-alert');
  const alertText = document.getElementById('vi-alert-text');
  if(village.alert === 'danger') {
    alertEl.className   = 'alert-row danger';
    alertText.textContent = '⚠ DANGER alert received from Village!';
  } else {
    alertEl.className   = 'alert-row safe';
    alertText.textContent = 'All clear — no active alerts';
  }
}

// ══════════════════════════════════════════════
//  TABS
// ══════════════════════════════════════════════
function switchTab(name) {
  document.querySelectorAll('.tab-btn').forEach((b,i) => {
    const names = ['village','control','log'];
    b.classList.toggle('active', names[i] === name);
  });
  document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
  document.getElementById('tab-'+name).classList.add('active');
}

// ══════════════════════════════════════════════
//  WEBSOCKET
// ══════════════════════════════════════════════
const WS_URL = 'ws://192.168.4.1:81';
let ws, reconnTimer;
let rxCount = 0, txCount = 0;

function connect() {
  ws = new WebSocket(WS_URL);
  ws.onopen = () => {
    setStatus(true);
    addLog('Connected to RANGER node','sys');
    document.querySelectorAll('button.kw').forEach(b => b.disabled = false);
  };
  ws.onclose = () => {
    setStatus(false);
    addLog('Disconnected — retrying in 3s...','sys');
    document.querySelectorAll('button.kw').forEach(b => b.disabled = true);
    clearTimeout(reconnTimer);
    reconnTimer = setTimeout(connect, 3000);
  };
  ws.onerror = () => addLog('WebSocket error','sys');
  ws.onmessage = e => {
    try {
      const d = JSON.parse(e.data);
      if(d.type === 'status') {
        if(d.uptime) document.getElementById('uptime-val').textContent = d.uptime;
        (d.history||[]).forEach(m => {
          addLog(m.text, m.direction==='FROM_VILLAGE'?'from':'to', m.time);
          if(m.direction==='FROM_VILLAGE') updateLastMsg(m.text, m.time);
        });
      } else if(d.type === 'message') {
        const cls = d.direction==='FROM_VILLAGE'?'from':'to';
        addLog(d.text, cls, d.time);
        if(cls==='from') {
          rxCount++;
          village.pktRx++;
          village.lastSeen = 0;
          updateLastMsg(d.text, d.time);
          // Check for alert keywords
          const txt = d.text.toLowerCase();
          village.alert = (txt.includes('danger')||txt.includes('fire')||txt.includes('help'))
                         ? 'danger' : 'safe';
          refreshVillagePanel();
        } else {
          txCount++;
          village.pktTx++;
        }
        document.getElementById('rx-count').textContent = rxCount;
        document.getElementById('tx-count').textContent = txCount;
      } else if(d.type==='ack') {
        flashButton(d.keyword);
      } else if(d.type==='error') {
        addLog('Error: '+d.msg,'sys');
      }
    } catch(ex){ console.warn(ex); }
  };
}

function setStatus(on) {
  const el = document.getElementById('ws-status');
  el.textContent = on ? '\u25CF Connected' : '\u25CF Disconnected';
  el.className   = on ? 'online' : 'offline';
}

function updateLastMsg(text, time) {
  document.getElementById('last-msg').textContent =
    '\u25BA '+text+(time?'  \u2022  '+time:'');
}

function addLog(text, cls, time) {
  const ul = document.getElementById('log');
  const li = document.createElement('li');
  li.className = cls;
  const badge = document.createElement('span');
  badge.className = 'badge '+cls;
  badge.textContent = cls==='from'?'VILLAGE':cls==='to'?'RANGER':'SYS';
  li.appendChild(badge);
  if(time){
    const t = document.createElement('span');
    t.style.cssText = 'opacity:.5;margin-right:5px;font-size:.73rem';
    t.textContent = '['+time+'] ';
    li.appendChild(t);
  }
  li.appendChild(document.createTextNode(text));
  ul.appendChild(li);
  ul.scrollTop = ul.scrollHeight;
  while(ul.children.length > 80) ul.removeChild(ul.firstChild);
}

function sendKW(kw) {
  if(!ws || ws.readyState !== WebSocket.OPEN){
    addLog('Not connected!','sys'); return;
  }
  ws.send(JSON.stringify({keyword:kw}));
  addLog('Sending keyword: '+kw,'to');
}

function flashButton(kw) {
  document.querySelectorAll('button.kw').forEach(b => {
    if(b.textContent.toLowerCase().includes(kw)){
      b.classList.add('active');
      setTimeout(()=>b.classList.remove('active'),900);
    }
  });
}

// ══════════════════════════════════════════════
//  UTILITIES
// ══════════════════════════════════════════════
function fmtTime(sec) {
  const h=Math.floor(sec/3600), m=Math.floor((sec%3600)/60), s=sec%60;
  return [h,m,s].map(n=>String(n).padStart(2,'0')).join(':');
}

// Local uptime ticker
setInterval(()=>{
  const el=document.getElementById('uptime-val');
  const txt=el.textContent;
  if(!txt||txt==='--:--:--') return;
  const p=txt.split(':').map(Number);
  p[2]++; if(p[2]>=60){p[2]=0;p[1]++;} if(p[1]>=60){p[1]=0;p[0]++;}
  el.textContent=p.map(n=>String(n).padStart(2,'0')).join(':');
},1000);

// Disable buttons until WS connects
document.querySelectorAll('button.kw').forEach(b=>b.disabled=true);
connect();
</script>
</body>
</html>
)rawhtml";

// ════════════════════════════════════════════════════════════════════════════
//  HTTP ROUTES
// ════════════════════════════════════════════════════════════════════════════
void handleRoot() {
  httpServer.setContentLength(
    strlen_P(HTML_PART1) + strlen_P(HTML_PART2)
  );
  httpServer.send(200, "text/html", "");
  httpServer.sendContent_P(HTML_PART1);
  httpServer.sendContent_P(HTML_PART2);
}

void handleNotFound() {
  // Redirect everything else to dashboard
  httpServer.sendHeader("Location", "/", true);
  httpServer.send(302, "text/plain", "");
}

// ════════════════════════════════════════════════════════════════════════════
//  LoRa RECEIVE
// ════════════════════════════════════════════════════════════════════════════
void checkLoRa() {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) return;

  String incoming = "";
  while (LoRa.available()) incoming += (char)LoRa.read();
  int rssi = LoRa.packetRssi();
  incoming.trim();

  Serial.printf("[LORA] RX (%d B, RSSI %d dBm): %s\n",
                packetSize, rssi, incoming.c_str());

  // Expected format from VILLAGE: MSG:some message text
  if (incoming.startsWith("MSG:")) {
    String msgText = incoming.substring(4);
    msgText.trim();
    if (msgText.length() > 0) {
      Serial.printf("[LORA] Village says: %s\n", msgText.c_str());
      broadcastMessage(msgText, "FROM_VILLAGE");
    }
  } else {
    Serial.printf("[LORA] Unknown packet ignored: %s\n", incoming.c_str());
  }
}

// ════════════════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n========== RANGER NODE BOOT ==========");

  // ── 1. WiFi Access Point ────────────────────────────────────────────────
  //    Clear any cached AP/STA config from NVS flash
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  delay(200);

  WiFi.mode(WIFI_AP);
  delay(100);

  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);

  //  channel 6, not hidden, max 4 clients
  bool apOk = WiFi.softAP(AP_SSID, AP_PASSWORD, 6, 0, 4);
  delay(500);   // AP needs ~500 ms to fully initialise

  if (apOk) {
    Serial.printf("[WIFI] Hotspot : %s\n", AP_SSID);
    Serial.printf("[WIFI] Password: %s\n", AP_PASSWORD);
    Serial.printf("[WIFI] IP      : %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("[WIFI] Channel : 6\n");
  } else {
    Serial.println("[WIFI] ERROR — softAP() failed!");
    Serial.println("[WIFI] Try: Tools → Erase All Flash → re-upload");
  }

  // ── 2. WebSocket server  (start BEFORE httpServer) ──────────────────────
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  Serial.println("[WS]   WebSocket server on port 81");

  // ── 3. HTTP server ───────────────────────────────────────────────────────
  httpServer.on("/", HTTP_GET, handleRoot);
  httpServer.onNotFound(handleNotFound);
  httpServer.begin();
  Serial.println("[HTTP] Server on port 80");

  // ── 4. LoRa ─────────────────────────────────────────────────────────────
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);

  int loraRetry = 0;
  while (!LoRa.begin(433E6)) {
    Serial.printf("[LORA] Init failed, retry %d/5...\n", ++loraRetry);
    delay(1000);
    if (loraRetry >= 5) {
      Serial.println("[LORA] FATAL: SX1278 not responding!");
      Serial.println("[LORA] Check: 3.3V supply, NSS=5, RST=14, DIO0=2");
      break;
    }
  }

  if (loraRetry < 5) {
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    LoRa.enableCrc();
    Serial.println("[LORA] Ready — 433 MHz | SF7 | BW125 | CR4/5 | CRC ON");
  }

  Serial.println("======================================\n");
}

// ════════════════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════════════════
void loop() {
  httpServer.handleClient();
  webSocket.loop();
  checkLoRa();
  yield();   // prevents WDT reset under heavy load
}
