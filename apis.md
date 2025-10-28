# Theia OLED Emulator — APIs

This document lists the public C++ APIs and HTTP/UDP interfaces exposed by the firmware modules present in the provided sources (`lcd_monitor`, `web_emu`, `wifimgr`, `led_stat`).

---

## Module: `lcd_monitor`

### Purpose
Emulates a US2066/HD44780-compatible **20×4** OLED at I²C address **0x3C**, tracks DDRAM/cursor state, and broadcasts snapshots of the screen over UDP as JSON.

### C++ API
```cpp
namespace LCDMonitor {
  void begin(int sda_pin, int scl_pin);
  void loop();

  void broadcastDisplayState(bool force = false);

  // Accessors / controls
  const LCDState& getDisplayState();
  void setEmulatorEnabled(bool enabled);
  bool isEmulatorEnabled();

  // Lifecycle helpers (present for future use)
  bool startI2CSniffer();
  void stopI2CSniffer();
  bool captureI2CTraffic();

  // Hooks (present but no-op in current build)
  struct I2CTransaction { uint8_t address; uint8_t data[32]; uint8_t length; uint32_t timestamp_ms; bool is_write; };
  void processI2CTransaction(const I2CTransaction& tx);
  void decodeLCDCommand(uint8_t addr, const uint8_t* data, uint8_t len);
}
```

### Key type
```cpp
struct LCDState {
  bool display_on, cursor_on, blink_on;
  uint8_t cursor_row, cursor_col;     // 0..3, 0..19
  char rows[4][21];                   // 20 chars + NUL
  uint8_t  detected_addr;             // typically 0x3C
  const char* controller_type;        // e.g., "US2066"
  bool initialized;
  uint32_t last_update_ms, packet_count;
};
```

### UDP Broadcast
- **Address**: `255.255.255.255` (broadcast)
- **Port**: `LCD_MONITOR_UDP_PORT` (constant in header; 35182 in current build)
- **Payload**: JSON (ArduinoJson-serialized), schema:
```json
{
  "type": "lcd20x4",
  "mode": "US2066",
  "addr": "0x3C",
  "disp": true,
  "cur": false,
  "blink": false,
  "cursor": { "r": 0, "c": 0 },
  "rows": [
    "Theia OLED Emulator",
    "Code:   Darkone83   ",
    "Team Resurgent      ",
    "(c) 2025            "
  ]
}
```
Notes:
- `rows` is always **4 strings**, each padded/truncated to **20** printable ASCII characters.
- A heartbeat and a short boot **burst** are emitted; otherwise updates are sent on content change (state hash).

---

## Module: `web_emu`

### Purpose
Serves the in-browser OLED emulator and exposes a JSON snapshot endpoint and an **SSE** stream for live updates.

### C++ API
```cpp
namespace WebEmu {
  void begin();
  void loop();
}
```

### HTTP Endpoints
- **GET `/emu`** — Web UI (HTML/JS/CSS).
- **GET `/emu/state`** — Current display state as JSON (subset of UDP schema; includes `type`, `disp`, `cur`, `blink`, `cursor`, `rows`).  
  Example:
  ```json
  {
    "type":"lcd20x4",
    "disp":true, "cur":false, "blink":false,
    "cursor":{"r":0,"c":0},
    "rows":["Theia OLED Emulator","Code:   Darkone83   ","Team Resurgent      ","(c) 2025            "]
  }
  ```
- **SSE `/emu/events`** — Server-Sent Events stream.  
  - Event `message`: payload is the same JSON snapshot as `/emu/state`.
  - Periodic keep-alives (`event: ka`) when idle.

### Client Notes
- UI offers **Skin**, **Pixel mode**, **Contrast** controls. It fetches `/emu/state` on load and subscribes to `/emu/events` for updates.

---

## Module: `wifimgr`

### Purpose
Captive-portal style Wi‑Fi provisioning (AP+STA), storing credentials in NVS, plus OTA upload and LCD emulator toggles.

### C++ API
```cpp
namespace WiFiMgr {
  AsyncWebServer& getServer();
  void begin();
  void loop();
  void restartPortal();
  void forgetWiFi();
  bool isConnected();
  String getStatus();
}
```

### Captive-Portal redirects
The following GET routes redirect to `/`:
```
/generate_204
/hotspot-detect.html
/redirect
/ncsi.txt
/captiveportal
```

### Wi‑Fi & Control Endpoints
- **GET `/`** — Portal page (HTML) with scan/connect UI and emulator on/off switch.
- **GET `/status`** — Text status string of current connection or portal mode.
- **GET `/scan`** — Returns `["ssid1","ssid2",...]` (deduplicated, sorted). Triggers background scan as needed.
- **GET `/connect?ssid=...&pass=...`** — Saves credentials and begins STA connection.
- **POST `/save`** — JSON body `{"ssid":"...","pass":"..."}`. Saves and begins STA connection.
- **GET `/forget`** — Clears saved credentials and returns to portal mode.
- **GET `/debug/forget`** — Same as `/forget` with extra serial logs.
- **POST `/reboot`** — Reboots the device after a short delay.

### LCD Emulator Control
- **GET `/lcd/state`** — `{ "enabled": true|false }`
- **ANY `/lcd/enable`** — Enables the I²C OLED emulator.
- **ANY `/lcd/disable`** — Disables the I²C OLED emulator (releases I²C slave).

### OTA
- **GET `/ota`** — Minimal OTA upload page (HTML with JS progress).
- **POST `/update`** — Multipart form field `firmware` (binary). On success responds `200 OK`. (No auto‑restart; use `/reboot`.)

---

## Module: `led_stat`

### Purpose
Single NeoPixel RGB LED indicating device status.

### C++ API
```cpp
enum class LedStatus { Booting, Portal, WifiConnected, WifiFailed, UdpTransmit };

namespace LedStat {
  void begin();
  void setStatus(LedStatus status);
  void loop();
}
```

### Behavior (defaults)
- `Booting` → solid white
- `Portal` → blinking purple
- `WifiConnected` → solid green
- `WifiFailed` → solid red
- `UdpTransmit` → brief activity indication (if used)

---

## Integration Cheatsheet

### Listen for screen updates (PC/receiver)
- Bind UDP socket to **port 35182**.
- Parse the JSON payload (see schema above).

### Browser client
- Initial snapshot: `GET /emu/state`
- Live updates: `EventSource('/emu/events')`

### Toggle emulator
```
GET /lcd/state
ANY /lcd/enable
ANY /lcd/disable
```

### Provision Wi‑Fi
```
GET  /scan
POST /save        body: {"ssid":"<ssid>","pass":"<pass>"}
GET  /connect?ssid=<ssid>&pass=<pass>
GET  /forget
GET  /status
```

### OTA
```
GET  /ota
POST /update   (multipart form data: firmware=<bin>)
POST /reboot
```

---

## Notes
- The emulator sanitizes non-printable characters to space (0x20) for UDP/HTTP emits.
- A small boot **burst** and periodic **keep-alives** ensure downstream listeners initialize correctly even if the screen is static.
