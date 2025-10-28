#include "lcd_monitor.h"
#include <Arduino.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <Wire.h>

static WiFiUDP lcdUdp;
static bool udp_begun = false;
static LCDMonitor::LCDState lcd_state;

// I2C slave configuration
static const uint8_t LCD_I2C_ADDRESS = 0x3C;
static bool i2c_slave_active = false;

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

    static void processHD44780Command(uint8_t cmd);
    static void processHD44780Data(uint8_t data);
    static void updateCursorPosition();
    static char translateHD44780Character(uint8_t code);
    static void ensureUdp();

    static void ensureUdp() {
        if (!udp_begun) {
            lcdUdp.begin(LCD_MONITOR_UDP_PORT);
            udp_begun = true;
        }
    }

    static char translateHD44780Character(uint8_t code) {
        if (code >= 0x20 && code <= 0x7E) {
            return (char)code;
        }
        return ' ';
    }

    static void updateCursorPosition() {
        if (ddram_address < 0x20) {
            lcd_state.cursor_row = 0;
            lcd_state.cursor_col = ddram_address;
        } else if (ddram_address >= 0x20 && ddram_address < 0x40) {
            lcd_state.cursor_row = 1;
            lcd_state.cursor_col = ddram_address - 0x20;
        } else if (ddram_address >= 0x40 && ddram_address < 0x60) {
            lcd_state.cursor_row = 2;
            lcd_state.cursor_col = ddram_address - 0x40;
        } else if (ddram_address >= 0x60 && ddram_address < 0x80) {
            lcd_state.cursor_row = 3;
            lcd_state.cursor_col = ddram_address - 0x60;
        } else {
            lcd_state.cursor_row = 0;
            lcd_state.cursor_col = 0;
        }
        
        if (lcd_state.cursor_row >= 4) lcd_state.cursor_row = 3;
        if (lcd_state.cursor_col >= 20) lcd_state.cursor_col = 19;
    }

    static void onI2CReceive(int numBytes) {
        Serial.printf("[LCD] RX: %d bytes\n", numBytes);
        
        while (Wire1.available() >= 2) {
            uint8_t control_byte = Wire1.read();
            uint8_t data_byte = Wire1.read();
            
            Serial.printf("[LCD] Control: 0x%02X, Data: 0x%02X\n", control_byte, data_byte);
            
            if (control_byte == 0x80) {
                Serial.printf("[LCD] -> Command: 0x%02X\n", data_byte);
                processHD44780Command(data_byte);
            } else if (control_byte == 0x40) {
                Serial.printf("[LCD] -> Character: 0x%02X '%c'\n", data_byte, 
                             (data_byte >= 0x20 && data_byte <= 0x7E) ? (char)data_byte : '?');
                processHD44780Data(data_byte);
            } else {
                Serial.printf("[LCD] -> Unknown control: 0x%02X\n", control_byte);
            }
        }
        
        if (Wire1.available() > 0) {
            uint8_t lone_byte = Wire1.read();
            Serial.printf("[LCD] WARNING: Lone byte: 0x%02X\n", lone_byte);
        }
        
        lcd_state.last_update_ms = millis();
    }

    static void onI2CRequest() {
        uint8_t status = 0x00 | (ddram_address & 0x7F);
        Wire1.write(status);
        Serial.printf("[LCD] Status: 0x%02X\n", status);
    }

    static void processHD44780Command(uint8_t cmd) {
        Serial.printf("[LCD] CMD: 0x%02X ", cmd);
        
        if (cmd == HD44780_CLEAR_DISPLAY) {
            for (int row = 0; row < 4; row++) {
                memset(lcd_state.rows[row], ' ', 20);
                lcd_state.rows[row][20] = '\0';
            }
            lcd_state.cursor_row = 0;
            lcd_state.cursor_col = 0;
            ddram_address = 0x00;
            Serial.println("(Clear)");

            broadcastDisplayState(true);
        }
        else if (cmd == HD44780_RETURN_HOME) {
            lcd_state.cursor_row = 0;
            lcd_state.cursor_col = 0;
            ddram_address = 0x00;
            Serial.println("(Home)");
        }
        else if ((cmd & 0x80) == HD44780_SET_DDRAM_ADDR) {
            ddram_address = cmd & 0x7F;
            updateCursorPosition();
            Serial.printf("(DDRAM: 0x%02X -> %d,%d)\n", ddram_address, 
                         lcd_state.cursor_row, lcd_state.cursor_col);
        }
        else if ((cmd & 0xF8) == HD44780_DISPLAY_CONTROL) {
            lcd_state.display_on = (cmd & 0x04) != 0;
            lcd_state.cursor_on = (cmd & 0x02) != 0;
            lcd_state.blink_on = (cmd & 0x01) != 0;
            Serial.printf("(Display: %s)\n", lcd_state.display_on ? "ON" : "OFF");
        }
        else {
            Serial.printf("(Other: 0x%02X)\n", cmd);
        }
    }

    static void processHD44780Data(uint8_t data) {
        if (lcd_state.cursor_row < 4 && lcd_state.cursor_col < 20) {
            char ch = translateHD44780Character(data);
            lcd_state.rows[lcd_state.cursor_row][lcd_state.cursor_col] = ch;
            
            Serial.printf("[LCD] '%c' at (%d,%d)\n", ch, lcd_state.cursor_row, lcd_state.cursor_col);
            
            lcd_state.cursor_col++;
            if (lcd_state.cursor_col >= 20) {
                lcd_state.cursor_col = 0;
                lcd_state.cursor_row++;
                if (lcd_state.cursor_row >= 4) {
                    lcd_state.cursor_row = 0;
                }
            }
            
            // Update DDRAM address to match cursor position
            int row_offsets[] = { 0x00, 0x20, 0x40, 0x60 };
            ddram_address = row_offsets[lcd_state.cursor_row] + lcd_state.cursor_col;
        }
    }

    static uint32_t s_last_hash = 0;

    static uint32_t computeStateHash(const LCDState& st) {
        uint32_t h = 2166136261u;
        auto mix = [&](uint8_t b){ h ^= b; h *= 16777619u; };

        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 20; ++c) {
                mix(static_cast<uint8_t>(st.rows[r][c]));
            }
        }
        mix(static_cast<uint8_t>(st.display_on));
        mix(static_cast<uint8_t>(st.cursor_on));
        mix(static_cast<uint8_t>(st.blink_on));
        mix(static_cast<uint8_t>(st.cursor_row));
        mix(static_cast<uint8_t>(st.cursor_col));
        mix(static_cast<uint8_t>(ddram_address));
        return h;
    }

    static uint8_t  s_boot_burst_left = 0;   
    static uint32_t s_boot_last_ms     = 0;  

    void begin(int sda_pin, int scl_pin) {
        g_sda_pin = sda_pin;
        g_scl_pin = scl_pin;

        lcd_state = LCDState();
        for (int row = 0; row < 4; row++) {
            memset(lcd_state.rows[row], ' ', 20);
            lcd_state.rows[row][20] = '\0';
        }
        
        strncpy(lcd_state.rows[0], "Theia OLED Emulator" , 20);
        strncpy(lcd_state.rows[1], "Code:   Darkone83   ", 20);
        strncpy(lcd_state.rows[2], "Team Resurgent      ", 20);
        strncpy(lcd_state.rows[3], "(c) 2025            ", 20);
        
        ddram_address = 0x00;
        lcd_state.detected_addr = LCD_I2C_ADDRESS;
        lcd_state.controller_type = "US2066";
        lcd_state.display_on = true;
        lcd_state.cursor_on = false;
        lcd_state.blink_on = false;

        if (!emulator_enabled) {
            i2c_slave_active = false;
            Serial.printf("[LCD] Emulator disabled at startup; I2C slave not started\n");
            broadcastDisplayState(true);
            return;
        }
        
        // Configure I2C slave
        Wire1.begin(LCD_I2C_ADDRESS, sda_pin, scl_pin, 0);
        Wire1.onReceive(onI2CReceive);
        Wire1.onRequest(onI2CRequest);
        
        i2c_slave_active = true;
        
        Serial.printf("[LCD] US2066 OLED emulator ready at 0x3C\n");

        // Prime a short boot burst so any listeners get a guaranteed full-frame
        s_boot_burst_left = 3; 
        s_boot_last_ms    = millis();

        broadcastDisplayState(true);
    }

    void loop() {
        static uint32_t last_send_ms = 0;
        static uint32_t last_heartbeat_ms = 0;

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

        if (s_boot_burst_left > 0) {
            const uint32_t now_ms = millis();
            if ((now_ms - s_boot_last_ms) >= 250) {
                broadcastDisplayState(true);
                s_boot_last_ms = now_ms;
                s_boot_burst_left--;
            }
            return;
        }

        const uint32_t h = computeStateHash(lcd_state);
        const bool changed   = (h != s_last_hash);
        const bool interval  = (now - last_send_ms) > 100;   
        const bool heartbeat = (now - last_heartbeat_ms) > 2000;

        if ((changed && interval) || heartbeat) {
            s_last_hash = h;
            last_send_ms = now;
            if (heartbeat) last_heartbeat_ms = now;
            broadcastDisplayState(/*force*/true);  
        }
    }

    void processI2CTransaction(const I2CTransaction& transaction) {
        
    }

    void decodeLCDCommand(uint8_t addr, const uint8_t* data, uint8_t len) {
        
    }

    void broadcastDisplayState(bool force) {
        ensureUdp();
        
        StaticJsonDocument<1024> doc;
        doc["type"] = "lcd20x4";
        doc["mode"] = "US2066";
        doc["addr"] = "0x3C";
        
        doc["disp"] = lcd_state.display_on;
        doc["cur"] = lcd_state.cursor_on;
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
                if (clean_row[j] < 0x20 || clean_row[j] > 0x7E) {
                    clean_row[j] = ' ';
                }
            }
            
            rows.add(clean_row);
        }
        
        String json_str;
        serializeJson(doc, json_str);
        
        lcdUdp.beginPacket(IPAddress(255,255,255,255), LCD_MONITOR_UDP_PORT);
        lcdUdp.print(json_str);
        lcdUdp.endPacket();
        
        if (force) {
            Serial.printf("[LCD] JSON: %s\n", json_str.c_str());
            Serial.printf("[LCD] Display:\n");
            for (int i = 0; i < 4; i++) {
                Serial.printf("  Row %d: \"", i);
                for (int j = 0; j < 20; j++) {
                    char c = lcd_state.rows[i][j];
                    if (c >= 0x20 && c <= 0x7E) {
                        Serial.printf("%c", c);
                    } else {
                        Serial.printf("?");
                    }
                }
                Serial.printf("\"\n");
            }
        }
    }

    const LCDState& getDisplayState() {
        return lcd_state;
    }

    bool startI2CSniffer() {
        return i2c_slave_active;
    }

    void stopI2CSniffer() {
        if (i2c_slave_active) {
            Wire1.end();
            i2c_slave_active = false;
        }
    }

    bool captureI2CTraffic() {
        return i2c_slave_active;
    }

    void setEmulatorEnabled(bool enabled) {
        emulator_enabled = enabled;
    }

    bool isEmulatorEnabled() {
        return emulator_enabled;
    }

} 
