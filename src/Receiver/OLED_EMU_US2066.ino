#include <Arduino.h>
#include <Wire.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include "wifimgr.h"     
#include "led_stat.h"    
#include "us2066.h"      

// ---------------- Constants (local) ----------------
static const uint16_t LCD_RX_UDP_PORT = 35182;
static const uint8_t  US2066_I2C_ADDR = 0x3C;    // US2066 default
static const char*    MDNS_HOST       = "oledemurec"; 

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif
#ifndef THEIA_BRAND_LINE
#define THEIA_BRAND_LINE "Theia Wireless Disp"
#endif

#define US2066_GLOBAL_COL_OFFSET -22   

// ---------------- Pins ----------------
static const int PIN_SDA = 6;
static const int PIN_SCL = 7;
static const int PIN_RST = -1;                   

static US2066LCD lcd;

// ---------------- Payload model (local) ----------------
struct LCD20x4State {
  bool     display_on = true;
  bool     cursor_on  = false;
  bool     blink_on   = false;
  uint8_t  cursor_row = 0;
  uint8_t  cursor_col = 0;
  char     rows[4][21] = {{0}};
  uint32_t last_update_ms = 0;
  bool     initialized = false;
};

static LCD20x4State g_state;
static bool         g_haveData = false;

// ---------------- Pages / UI ----------------
enum class Page : uint8_t { Splash=0, Live=1, Info=2 };
static Page currentPage = Page::Splash;

static const uint32_t INFO_INTERVAL_MS = 60000;
static const uint32_t INFO_DURATION_MS = 10000;
static uint32_t lastInfoShownMs = 0;
static uint32_t infoStartMs     = 0;

static WiFiUDP g_udp;
static bool    g_udpBegun   = false;
static bool    g_mdnsReady  = false;

volatile uint32_t g_udpPacketCount = 0;

static inline String fit20(const String& s) {
  if (s.length() >= 20) return s.substring(0, 20);
  String out(s);
  while (out.length() < 20) out += ' ';
  return out;
}

static String uptimeHMS(uint32_t ms) {
  uint32_t s = ms / 1000;
  uint32_t h = s / 3600; s %= 3600;
  uint32_t m = s / 60;   s %= 60;
  char buf[9]; // "HH:MM:SS"
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
  return String(buf);
}

static void ensureUdp() {
  if (!g_udpBegun) {
    g_udp.begin(LCD_RX_UDP_PORT);
    g_udpBegun = true;
  }
}

static void ensureMdns() {
  if (!g_mdnsReady && WiFiMgr::isConnected()) {
    WiFi.setHostname(MDNS_HOST);
    if (!MDNS.begin(MDNS_HOST)) {
      return;
    }
    MDNS.addService("oledemurec", "udp", LCD_RX_UDP_PORT);
    g_mdnsReady = true;
  }
}

static bool parse_lcd20x4(const String& payload, LCD20x4State& out) {
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;

  const char* type = doc["type"] | "";
  if (strcmp(type, "lcd20x4") != 0) return false;

  out.display_on = doc["disp"]  | true;
  out.cursor_on  = doc["cur"]   | false;
  out.blink_on   = doc["blink"] | false;

  JsonObject cur = doc["cursor"];
  out.cursor_row = cur["r"] | 0;
  out.cursor_col = cur["c"] | 0;

  JsonArray rows = doc["rows"];
  if (!rows.isNull() && rows.size() >= 4) {
    for (int i = 0; i < 4; ++i) {
      const char* r = rows[i] | "";
      size_t n = strnlen(r, 20);
      memset(out.rows[i], ' ', 20);
      memcpy(out.rows[i], r, n);
      out.rows[i][20] = '\0';
      for (int j = 0; j < 20; ++j) {
        if (out.rows[i][j] < 0x20 || out.rows[i][j] > 0x7E) out.rows[i][j] = ' ';
      }
    }
  }

  out.last_update_ms = millis();
  out.initialized    = true;
  return true;
}

static void draw_splash() {
  lcd.displayOn(false, false);
  lcd.clear();
  lcd.writeRow(0, THEIA_BRAND_LINE);
  lcd.writeRow(1, "Waiting for data...");

  String s = WiFiMgr::isConnected()
               ? ("IP " + WiFi.localIP().toString())
               : String("Portal: 192.168.4.1");
  lcd.writeRow(2, s);
  lcd.writeRow(3, " 2025 Team Resurgent");

  lcd.displayOn(true, false);
}

static void draw_live(const LCD20x4State& st) {
  lcd.displayOn(st.cursor_on, st.blink_on);
  for (int i = 0; i < 4; ++i) lcd.writeRow(i, st.rows[i]);
}

static void draw_theia_info_page() {
  lcd.writeRow(0, fit20(THEIA_BRAND_LINE));
  lcd.writeRow(1, fit20(String("FW ") + FIRMWARE_VERSION));

  String l2;
  if (WiFi.status() == WL_CONNECTED) {
    String ssid = WiFi.SSID();
    if (ssid.length() > 15) ssid = ssid.substring(0, 15); // "WiFi:" + 15 = 20
    l2 = "WiFi:" + ssid;
  } else {
    IPAddress ip = WiFi.localIP();
    if (ip == (uint32_t)0) ip = WiFi.softAPIP();
    l2 = "IP " + ip.toString();
  }
  lcd.writeRow(2, fit20(l2));

  String up  = uptimeHMS(millis());
  String cnt = String(g_udpPacketCount);
  String l3  = "UP " + up + " N:" + cnt;
  if (l3.length() > 20) {
    l3 = "UP" + up + " N:" + cnt;
    if (l3.length() > 20) {
      int maxDigits = 20 - 2 - up.length() - 3; // "UP" + up + " N:"
      if (maxDigits < 1) maxDigits = 1;
      l3 = "UP" + up + " N:" + cnt.substring(0, maxDigits);
    }
  }
  lcd.writeRow(3, fit20(l3));
}

void setup() {
  Serial.begin(115200);
  delay(50);

  LedStat::begin();
  WiFiMgr::begin();

  lcd.begin(PIN_SDA, PIN_SCL, PIN_RST, US2066_I2C_ADDR);

  ensureUdp();

  currentPage = Page::Splash;
  draw_splash();
}

static uint32_t lastPaintMs = 0;
static uint32_t lastSplashRefreshMs = 0;

void loop() {
  WiFiMgr::loop();
  LedStat::loop();

  ensureMdns();
  ensureUdp();

  int pkt = g_udp.parsePacket();
  if (pkt > 0) {
    String payload; payload.reserve(min(pkt, 1024));
    while (g_udp.available()) payload += (char)g_udp.read();

    LCD20x4State tmp;
    if (parse_lcd20x4(payload, tmp)) {
      g_state     = tmp;
      g_haveData  = true;
      g_udpPacketCount++;           
      currentPage = Page::Live;     
      LedStat::setStatus(LedStatus::UdpTransmit);
    }
  }

  uint32_t now = millis();
  if (now - lastPaintMs >= 100) {
    lastPaintMs = now;

    if (!g_haveData) {
      if (now - lastSplashRefreshMs > 1000) {
        lastSplashRefreshMs = now;
        draw_splash();
      }
    } else {
      if (currentPage != Page::Info) {
        if ((now - lastInfoShownMs) >= INFO_INTERVAL_MS) {
          currentPage   = Page::Info;
          infoStartMs   = now;
          lastInfoShownMs = now;
        }
      } else {
        if ((now - infoStartMs) >= INFO_DURATION_MS) {
          currentPage = Page::Live;
        }
      }

      if (currentPage == Page::Live) {
        draw_live(g_state);
      } else if (currentPage == Page::Info) {
        draw_theia_info_page();
      }
    }
  }
}
