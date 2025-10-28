

#pragma once

#include <stdint.h>

// LCD I2C addresses to monitor
#define LCD_PCF8574_ADDR    0x27  // PCF8574 I2C backpack
#define LCD_US2066_ADDR     0x3C  // US2066/SSD1311/HD22780 OLED controller

// UDP port for LCD data transmission
#ifndef LCD_MONITOR_UDP_PORT
#define LCD_MONITOR_UDP_PORT 35182
#endif

namespace LCDMonitor {

    struct I2CTransaction {
        uint8_t address;
        uint8_t data[32];
        uint8_t length;
        uint32_t timestamp_ms;
        bool is_write;
    };

    struct LCDState {
        bool display_on = false;
        bool cursor_on = false;
        bool blink_on = false;
        uint8_t cursor_row = 0;
        uint8_t cursor_col = 0;

        char rows[4][21] = {{0}};

        uint8_t detected_addr = 0;
        const char* controller_type = "UNKNOWN";

        bool initialized = false;
        uint32_t last_update_ms = 0;
        uint32_t packet_count = 0;
    };

    void begin(int sda_pin, int scl_pin);

    void loop();

    void processI2CTransaction(const I2CTransaction& transaction);

    void decodeLCDCommand(uint8_t addr, const uint8_t* data, uint8_t len);

    void broadcastDisplayState(bool force = false);

    const LCDState& getDisplayState();

    void setEmulatorEnabled(bool enabled);
    bool isEmulatorEnabled();

    bool startI2CSniffer();
    void stopI2CSniffer();
    bool captureI2CTraffic();
}
