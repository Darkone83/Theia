<a href="https://discord.gg/k2BQhSJ"><img src="https://github.com/Darkone83/ModXo-Basic/blob/main/Images/discord.svg"></a>

# Theia – OLED Web Emulator for Original Xbox

A Wi-Fi enabled ESP32-S3 emulator for the Xbox 20x4 US2066/HD44780 display.

## Overview

Theia is a lightweight firmware for the ESP32-S3 that emulates the Original Xbox 20x4 OLED/HD44780 display. It provides a browser-based LCD viewer, Wi-Fi setup portal, OTA firmware updates, and UDP/SSE LCD state streaming for PC or Mobile apps.

## Features

### Wi-Fi Setup Portal
- Connect to Wi-Fi
- Forget or update credentials
- Auto portal mode when no credentials are saved

### 20x4 OLED / LCD Emulation
- Emulates a US2066-compatible display at I2C address 0x3C
- Supports HD44780 commands and data
- Tracks cursor and row content
- Can be toggled on or off at runtime

### Web-Based LCD Viewer
Live LCD preview available at:
  /emu

JSON snapshot available at:

  /emu/state

### OTA Firmware Updates
Upload new firmware (.bin) at:

  /ota

### RGB Status LED
Indicates:
- Booting
- Wi-Fi connected
- Portal mode
- Wi-Fi failure
- UDP transmit activity

---

## Compiling the Firmware

1. Install **Arduino IDE 2.x**).
2. Install the **ESP32 board package**:
   - Arduino: Boards Manager → search for "esp32" → install `esp32 by Espressif Systems`
   - PlatformIO: Use the `esp32-s3` platform
3. Select the correct board:
   - `ESP32S3 Dev Module` (or your S3 variant)
4. Set the USB mode:
   - Tools → USB Mode → USB-CDC On Boot: **Enabled**
5. Open the `OLED_EMU.ino`, or `OLED_EMU_US2066.ino` file.
6. Ensure required libraries are installed:
   - ESPAsyncWebServer  
   - AsyncTCP  
   - ArduinoJson
7. Connect your ESP32-S3 board via USB.
8. Click **Upload** to compile and flash the firmware.

If compilation fails, verify the board package version and that ESPAsyncWebServer is the S3-compatible version.

---

## Hardware Installation
(To be completed by the user.)

## Getting Started

1. Flash the ESP32-S3 with the firmware.
2. Power the device. It starts in setup mode if Wi-Fi is not configured.
3. Connect to the access point:
   Type D OLED EMU Setup
4. Open a browser and go to:
   http://oledemu.local, or http://192.168.4.1
5. Save Wi-Fi credentials.
6. After connecting:
   Web Emulator: http://oledemu.local/emu
   JSON State:   http://oledemu.local/emu/state
   OTA Update:   http://olecemu.local/ota

## UDP Output

LCD state is broadcast as JSON over UDP on port 35182 following the Type-D viewer format.

## Compatibility

- Original Xbox (all revisions)
- PrometheOS / HD44780 LCD systems
- Type-D Viewer (PC)
- Their (iOS / Android)

## Known Good Boards

- ESP32-S3 DevKit
- Waveshare ESP32-S3 LCD boards
- Standard S3 Mini modules

## Credits

Developed by Darkone83 / Team Resurgent.
