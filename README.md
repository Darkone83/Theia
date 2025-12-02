<a href="https://discord.gg/k2BQhSJ"><img src="https://github.com/Darkone83/ModXo-Basic/blob/main/Images/discord.svg"></a>

<div align=center>
  <img src="https://github.com/Darkone83/Theia/blob/main/images/theia.png" width=400>

  <img src="https://github.com/Darkone83/Theia/blob/main/images/DC%20logo.png" width=200><img src="https://github.com/Darkone83/Theia/blob/main/images/team-resurgent.png" width=200>
</div>


# Theia – OLED Web Emulator for Original Xbox

A Wi-Fi enabled ESP32-S3 emulator for the Xbox LCD display output.

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

### US2066 Support
Wireless transmission to a NewHaven US2066 OLED for actual hardware support with the supported backpack

---

## Purchase:

OLED Emulator module: Darkone Customs (coming soon!)

OLED kit: <a href="https://www.darkonecustoms.com/store/p/theia-wireless-oled-emulator">Dakrone Customs</a>

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

Transmitter:

Its recommented to wire directly to the LPC if your XBOX and to keep the wire runs as short as possible

| Pin | Transmiter | XBOX LPC |
|-----|------------|----------|
|  1  |     5V     | 5V Pin 6 |
|  2  |    GND     | GND Pin 2 or 12 |
|  3  |   3.3V     | 3.3V Pin 9 or 13 |
|  4  |    SDA     | SDA Pin 14 |
|  5  |    SCL     | SCL Pin 13 |


Transmitter Pins:

<img src="https://github.com/Darkone83/Theia/blob/main/images/pins.png" width=300>

XBOX LPC Pins:

<img src="https://github.com/Darkone83/Theia/blob/main/images/lpc_pinouts.png" width=300>

Reciever:

**Notes:** IF getting your OWN PCB's made select 1.0mm thickness for the US2066 backpack; otherwise, you may have fitment issues. This os only compatable with the NewHaven Display:  NHD-0420CW

Solder your backpack to the display, then connect 5V and GND to the controller port

When inserting into the case please take note of the TPU spacer placement

<img src="https://github.com/Darkone83/Theia/blob/main/images/TPU%20placement.jpg" width=400>

## Getting Started

1. Flash the ESP32-S3 with the firmware.
2. Power the device. It starts in setup mode if Wi-Fi is not configured.
3. Connect to the access point:
   Type D OLED EMU Setup
4. Open a browser and go to:
   http://oledemu.local, or http://192.168.4.1 for the Transmitter, for the Receiver go to: http://oledemurec.local, ot http://192.168.4.1
5. Save Wi-Fi credentials.
6. After connecting:
   Web Emulator: http://oledemu.local/emu
   JSON State:   http://oledemu.local/emu/state
   OTA Update:   http://oledemu.local/ota

### XBOX Settings

PrometheOS:

Select SMBUS, HD47880 and set address to 0x3C (all other settings can be left alone)

Cerbios:

Select SMBUS, HD47880 and set address to 0x3C (all other settings can be left alone)

XBMC4Gamers:

Select SMBUS, HD47880 and set address to 0x3C (all other settings can be left alone)

## UDP Output

LCD state is broadcast as JSON over UDP on port 35182 following the Type-D viewer format.

## Compatibility

- Original Xbox (all revisions)
- PrometheOS / HD44780 LCD systems
- Type-D Viewer (PC)
- Theia (iOS / Android)

## Known Good Boards

- ESP32-S3 DevKit
- Waveshare ESP32-S3 LCD boards
- Standard S3 Mini modules

## Credits

Developed by Darkone83 / Team Resurgent.
