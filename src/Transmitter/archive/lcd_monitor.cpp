#include "lcd_monitor.h"
#include <Arduino.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <WiFi.h>  // <-- ADD

// Private state
static WiFiUDP lcdUdp;
static bool udp_begun = false;
static LCDMonitor::LCDState lcd_state;

// I2C slave configuration
static const uint8_t LCD_I2C_ADDRESS = 0x3C;
static bool i2c_slave_active = false;

// ---- runtime emulator flag + remembered pins ----
static volatile bool emulator_enabled = true;
static int g_sda_pin = -1;
static int g_scl_pin = -1;

// HD44780 state
static uint8_t ddram_address = 0x00;

// HD44780 Commands
#define HD44780_CLEAR_DISPLAY     0x01
#define HD44780_RETURN_HOME       0x02
#define HD44780_ENTRY_MODE_SET    0x04
#define HD44780_DISPLAY_CONTROL   0x08
#define HD44780_CURSOR_SHIFT      0x10
#define HD44780_FUNCTION_SET      0x20
#define HD44780_SET_CGRAM_ADDR    0x40
#define HD44780_SET_DDRAM_ADDR    0x80

namespace LCDMonitor {

    // Forward declarations
    static void processHD44780Command(uint8_t cmd);
    static void processHD44780Data(uint8_t data);
    static void updateCursorPosition();
    static char translateHD44780Character(uint8_t code);

    // ---- helpers for UDP send on multiple interfaces ----
    static inline IPAddress calcBroadcast(IPAddress ip, IPAddress mask) {
        // broadcast = ip | (~mask)
        uint32_t ip32 = (uint32_t)ip;
        uint32_t m32  = (uint32_t)mask;
        uint32_t b32  = ip32 | ~m32;
        return IPAddress(b32);
    }

    static bool udpSendTo(const IPAddress& dst, uint16_t port, const String& payload) {
        if (!lcdUdp.beginPacket(dst, port)) {
            Serial.printf("[LCD][ERR] beginPacket() failed to %s:%u\n", dst.toString().c_str(), port);
            return false;
        }
        lcdUdp.write((const uint8_t*)payload.c_str(), payload.length());
        if (!lcdUdp.endPacket()) {
            Serial.printf("[LCD][ERR] endPacket() failed to %s:%u\n", dst.toString().c_str(), port);
            return false;
        }
        return true;
    }

    static void ensureUdp() {
        if (!udp_begun) {
            // Bind to ephemeral local port; we only TRANSMIT.
            if (!lcdUdp.begin(0)) {
                Serial.println("[LCD][ERR] WiFiUDP.begin(0) failed");
            } else {
                Serial.println("[LCD] WiFiUDP bound (ephemeral src port)");
            }
            udp_begun = true;
        }
    }

    // Simple ASCII character translation
    static char translateHD44780Character(uint8_t code) {
        if (code >= 0x20 && code <= 0x7E) return (char)code;
        return ' ';
    }

    // Update cursor position from DDRAM address
    static void updateCursorPosition() {
        if (ddram_address < 0x20) {
            lcd_state.cursor_row = 0;
            lcd_state.cursor_col = ddram_address;
        } else if (ddram_address < 0x40) {
            lcd_state.cursor_row = 1;
            lcd_state.cursor_col = ddram_address - 0x20;
        } else if (ddram_address < 0x60) {
            lcd_state.cursor_row = 2;
            lcd_state.cursor_col = ddram_address - 0x40;
        } else if (ddram_address < 0x80) {
            lcd_state.cursor_row = 3;
            lcd_state.cursor_col = ddram_address - 0x60;
        } else {
            lcd_state.cursor_row = 0;
            lcd_state.cursor_col = 0;
        }
        if (lcd_state.cursor_row >= 4) lcd_state.cursor_row = 3;
        if (lcd_state.cursor_col >= 20) lcd_state.cursor_col = 19;
    }

    // US2066/OLED I2C handler
    static void onI2CReceive(int numBytes) {
        Serial.printf("[LCD] RX: %d bytes\n", numBytes);
        while (Wire1.available() >= 2) {
            uint8_t control_byte = Wire1.read();
            uint8_t data_byte    = Wire1.read();

            if (control_byte == 0x80) {
                processHD44780Command(data_byte);
            } else if (control_byte == 0x40) {
                processHD44780Data(data_byte);
            } else {
                Serial.printf("[LCD] Unknown ctl=0x%02X data=0x%02X\n", control_byte, data_byte);
            }
        }
        if (Wire1.available() > 0) {
            uint8_t lone = Wire1.read();
            Serial.printf("[LCD] WARNING: Lone byte: 0x%02X\n", lone);
        }
        lcd_state.last_update_ms = millis();
    }

    static void onI2CRequest() {
        uint8_t status = 0x00 | (ddram_address & 0x7F);
        Wire1.write(status);
    }

    static void processHD44780Command(uint8_t cmd) {
        if (cmd == HD44780_CLEAR_DISPLAY) {
            for (int row = 0; row < 4; row++) {
                memset(lcd_state.rows[row], ' ', 20);
                lcd_state.rows[row][20] = '\0';
            }
            lcd_state.cursor_row = 0;
            lcd_state.cursor_col = 0;
            ddram_address = 0x00;
        }
        else if (cmd == HD44780_RETURN_HOME) {
            lcd_state.cursor_row = 0;
            lcd_state.cursor_col = 0;
            ddram_address = 0x00;
        }
        else if ((cmd & 0x80) == HD44780_SET_DDRAM_ADDR) {
            ddram_address = cmd & 0x7F;
            updateCursorPosition();
        }
        else if ((cmd & 0xF8) == HD44780_DISPLAY_CONTROL) {
            lcd_state.display_on = (cmd & 0x04) != 0;
            lcd_state.cursor_on  = (cmd & 0x02) != 0;
            lcd_state.blink_on   = (cmd & 0x01) != 0;
        }
    }

    static void processHD44780Data(uint8_t data) {
        if (lcd_state.cursor_row < 4 && lcd_state.cursor_col < 20) {
            char ch = translateHD44780Character(data);
            lcd_state.rows[lcd_state.cursor_row][lcd_state.cursor_col] = ch;

            lcd_state.cursor_col++;
            if (lcd_state.cursor_col >= 20) {
                lcd_state.cursor_col = 0;
                lcd_state.cursor_row++;
                if (lcd_state.cursor_row >= 4) lcd_state.cursor_row = 0;
            }
            const int row_offsets[] = { 0x00, 0x20, 0x40, 0x60 };
            ddram_address = row_offsets[lcd_state.cursor_row] + lcd_state.cursor_col;
        }
    }

    void begin(int sda_pin, int scl_pin) {
        g_sda_pin = sda_pin;
        g_scl_pin = scl_pin;

        lcd_state = LCDState();
        for (int row = 0; row < 4; row++) {
            memset(lcd_state.rows[row], ' ', 20);
            lcd_state.rows[row][20] = '\0';
        }

        strncpy(lcd_state.rows[0], "Theia OLED Emulator ", 20);
        strncpy(lcd_state.rows[1], "Code:   Darkone83   ", 20);
        strncpy(lcd_state.rows[2], "Team Resurgent      ", 20);
        strncpy(lcd_state.rows[3], "(c) 2025            ", 20);

        ddram_address = 0x00;
        lcd_state.detected_addr   = LCD_I2C_ADDRESS;
        lcd_state.controller_type = "US2066";
        lcd_state.display_on = true;
        lcd_state.cursor_on  = false;
        lcd_state.blink_on   = false;

        if (!emulator_enabled) {
            i2c_slave_active = false;
            Serial.println("[LCD] Emulator disabled at startup; I2C slave not started");
            broadcastDisplayState(true);
            return;
        }

        Wire1.begin(LCD_I2C_ADDRESS, sda_pin, scl_pin, 0);
        Wire1.onReceive(onI2CReceive);
        Wire1.onRequest(onI2CRequest);
        i2c_slave_active = true;

        Serial.println("[LCD] US2066 OLED emulator ready at 0x3C");
        broadcastDisplayState(true);
    }

    void loop() {
        static uint32_t last_broadcast = 0;
        const uint32_t now = millis();

        if (!emulator_enabled) {
            if (i2c_slave_active) {
                Wire1.end();
                i2c_slave_active = false;
                Serial.println("[LCD] Emulator disabled -> I2C slave stopped");
            }
            return;
        } else if (!i2c_slave_active && g_sda_pin >= 0 && g_scl_pin >= 0) {
            Wire1.begin(LCD_I2C_ADDRESS, g_sda_pin, g_scl_pin, 0);
            Wire1.onReceive(onI2CReceive);
            Wire1.onRequest(onI2CRequest);
            i2c_slave_active = true;
            Serial.println("[LCD] Emulator enabled -> I2C slave started");
        }

        if (lcd_state.last_update_ms > last_broadcast && (now - last_broadcast) > 1000) {
            broadcastDisplayState(false);
            last_broadcast = now;
            return;
        }

        // Optional heartbeat to prevent "looks dead" when I2C idle
        if ((now - last_broadcast) > 2000) {
            broadcastDisplayState(false);
            last_broadcast = now;
        }
    }

    void processI2CTransaction(const I2CTransaction&) {}
    void decodeLCDCommand(uint8_t, const uint8_t*, uint8_t) {}

    void broadcastDisplayState(bool force) {
        ensureUdp();

        StaticJsonDocument<1024> doc;
        doc["type"]  = "lcd20x4";
        doc["mode"]  = "US2066";
        doc["addr"]  = "0x3C";
        doc["disp"]  = lcd_state.display_on;
        doc["cur"]   = lcd_state.cursor_on;
        doc["blink"] = lcd_state.blink_on;

        JsonObject cursor = doc.createNestedObject("cursor");
        cursor["r"] = lcd_state.cursor_row;
        cursor["c"] = lcd_state.cursor_col;

        JsonArray rows = doc.createNestedArray("rows");
        for (int i = 0; i < 4; i++) {
            char clean_row[21];
            memcpy(clean_row, lcd_state.rows[i], 20);
            clean_row[20] = '\0';
            for (int j = 0; j < 20; j++) {
                if (clean_row[j] < 0x20 || clean_row[j] > 0x7E) clean_row[j] = ' ';
            }
            rows.add(clean_row);
        }

        String json_str;
        serializeJson(doc, json_str);

        // ---- Send to all relevant broadcasts ----
        bool any = false;

        // 1) Global limited broadcast (usually routed to one iface)
        any |= udpSendTo(IPAddress(255,255,255,255), LCD_MONITOR_UDP_PORT, json_str);

        // 2) STA broadcast (if STA up)
        if (WiFi.getMode() & WIFI_MODE_STA) {
            IPAddress staIP   = WiFi.localIP();
            IPAddress staMask = WiFi.subnetMask();
            if (staIP != IPAddress(0,0,0,0)) {
                IPAddress staBcast = calcBroadcast(staIP, staMask);
                any |= udpSendTo(staBcast, LCD_MONITOR_UDP_PORT, json_str);
            }
        }

        // 3) AP broadcast (if AP up). You set mask 255.255.255.0 in startPortal()
        if (WiFi.getMode() & WIFI_MODE_AP) {
            IPAddress apIP   = WiFi.softAPIP();
            if (apIP != IPAddress(0,0,0,0)) {
                IPAddress apBcast = IPAddress(apIP[0], apIP[1], apIP[2], 255);
                any |= udpSendTo(apBcast, LCD_MONITOR_UDP_PORT, json_str);
            }
        }

        if (!any) {
            Serial.println("[LCD][WARN] UDP send: no interface accepted packet (AP/STA down?)");
        }

        if (force) {
            Serial.printf("[LCD] JSON: %s\n", json_str.c_str());
        }
    }

    const LCDState& getDisplayState() { return lcd_state; }

    bool startI2CSniffer() { return i2c_slave_active; }

    void stopI2CSniffer() {
        if (i2c_slave_active) {
            Wire1.end();
            i2c_slave_active = false;
        }
    }

    bool captureI2CTraffic() { return i2c_slave_active; }

    void setEmulatorEnabled(bool enabled) { emulator_enabled = enabled; }
    bool isEmulatorEnabled() { return emulator_enabled; }

} // namespace LCDMonitor
