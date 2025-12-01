// lcd_monitor.h
//
// I2C LCD Traffic Monitor for Original Xbox Type-D firmware
// Passively monitors I2C bus traffic for LCD controllers at 0x27 (PCF8574) and 0x3C (US2066)
// Captures and decodes LCD commands/data, forwards to Python script via UDP
//
// By default this is a PASSIVE monitor (sniffer). Firmware may also run an ACTIVE
// US2066/HD44780 emulator as an I2C slave at 0x3C. Use setEmulatorEnabled()/isEmulatorEnabled()
// to toggle the emulator safely at runtime (e.g., from WiFiMgr).

#pragma once

#include <stdint.h>

// LCD I2C addresses to monitor
#define LCD_PCF8574_ADDR    0x27  // PCF8574 I2C backpack
#define LCD_US2066_ADDR     0x3C  // US2066/SSD1311 OLED controller

// UDP port for LCD data transmission
#ifndef LCD_MONITOR_UDP_PORT
#define LCD_MONITOR_UDP_PORT 35182
#endif

namespace LCDMonitor {

    // I2C transaction record
    struct I2CTransaction {
        uint8_t address;
        uint8_t data[32];
        uint8_t length;
        uint32_t timestamp_ms;
        bool is_write;
    };

    // LCD display state (decoded from traffic)
    struct LCDState {
        // Display configuration
        bool display_on = false;
        bool cursor_on = false;
        bool blink_on = false;
        uint8_t cursor_row = 0;
        uint8_t cursor_col = 0;

        // Display content (20x4 max)
        char rows[4][21] = {{0}};  // 20 chars + null terminator

        // Controller info
        uint8_t detected_addr = 0;
        const char* controller_type = "UNKNOWN";

        // Status
        bool initialized = false;
        uint32_t last_update_ms = 0;
        uint32_t packet_count = 0;
    };

    // Initialize the LCD monitor (sets up sniffer and/or emulator pins)
    void begin(int sda_pin, int scl_pin);

    // Main loop function - processes captured I2C traffic and periodic broadcasts
    void loop();

    // Process captured I2C transaction
    void processI2CTransaction(const I2CTransaction& transaction);

    // Decode LCD command for specific controller type
    void decodeLCDCommand(uint8_t addr, const uint8_t* data, uint8_t len);

    // Send current display state via UDP
    void broadcastDisplayState(bool force = false);

    // Get current display state (read-only)
    const LCDState& getDisplayState();

    // ---- Runtime control (for WiFiMgr / settings) ----
    // Enable/disable the active US2066/HD44780 emulator at 0x3C on the fly.
    // When disabled: the I2C slave is torn down (bus released).
    // When enabled:  the I2C slave is (re)started using the pins passed to begin().
    void setEmulatorEnabled(bool enabled);
    bool isEmulatorEnabled();

    // I2C sniffer functions
    bool startI2CSniffer();
    void stopI2CSniffer();
    bool captureI2CTraffic();
}
