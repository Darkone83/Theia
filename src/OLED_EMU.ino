#include "wifimgr.h"
#include "led_stat.h"
#include <Wire.h>
#include "lcd_monitor.h"
#include <ESPmDNS.h>

// ====== Hardware pins (set to your wiring) ======
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 7
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 6
#endif


void setup() {
  LedStat::begin();
  LedStat::setStatus(LedStatus::Booting);

  Serial.begin(115200);
  delay(150); // let USB CDC settle a bit

  WiFiMgr::begin();

  LCDMonitor::begin(I2C_SDA_PIN, I2C_SCL_PIN);

  Serial.println("[Main] Type-D OLED Emulator started.");
}

void loop() {
  // lightweight background services
  LedStat::loop();
  WiFiMgr::loop();
      // Bring up mDNS and UDP once Wi-Fi is connected
    static bool mdnsStarted = false;
    static bool udpEnabled = false;
    const bool connected = WiFiMgr::isConnected();

    if (connected && !mdnsStarted) {
        if (MDNS.begin("oledemu")) {
            Serial.println("[mDNS] Started: http://oledemu.local/");
            mdnsStarted = true;
        } else {
            Serial.println("[mDNS] mDNS start failed");
        }
    }

  if (WiFiMgr::isConnected()) {
    LCDMonitor::loop();
  }

  delay(1);
}
