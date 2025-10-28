#include "us2066.h"
#include <Wire.h>
#include <WiFiUdp.h>

// --------- Base HD44780/US2066 command bits ----------
static constexpr uint8_t CMD_DISPLAY_CTRL = 0x08;
static constexpr uint8_t BIT_DISPLAY_ON   = 0x04;
static constexpr uint8_t BIT_CURSOR_ON    = 0x02;
static constexpr uint8_t BIT_BLINK_ON     = 0x01;

static constexpr uint8_t CMD_ENTRY_MODE   = 0x04;
static constexpr uint8_t BIT_ENTRY_INC    = 0x02;

static constexpr uint8_t CMD_CLEAR        = 0x01;
static constexpr uint8_t CMD_HOME         = 0x02;
static constexpr uint8_t CMD_SET_DDRAM    = 0x80;

// US2066 extended / OLED command-set entry/exit
static constexpr uint8_t CMD_FUNCSET_RE1  = 0x2A; // RE=1
static constexpr uint8_t CMD_OLED_ON      = 0x79; // SD=1
static constexpr uint8_t CMD_OLED_OFF     = 0x78; // SD=0
static constexpr uint8_t CMD_FUNCSET_RE0  = 0x28; // RE=0

// US2066 Set Contrast (double-byte) inside OLED cmd-set
static constexpr uint8_t CMD_SET_CONTRAST = 0x81;

static inline void delayShort() { delayMicroseconds(60); } 
static inline void delayLong()  { delay(2); }              

static inline bool i2cSend2(uint8_t addr, uint8_t b0, uint8_t b1, bool &err) {
  Wire.beginTransmission(addr);
  Wire.write(b0);
  Wire.write(b1);
  uint8_t r = Wire.endTransmission();
  err |= (r != 0);
  delayShort();
  return (r == 0);
}

static inline bool i2cSendBlock(uint8_t addr, uint8_t ctrl, const uint8_t* data, size_t len, bool &err) {
  if (!data || !len) return true;
  const size_t CHUNK = 8; 
  size_t off = 0;
  while (off < len) {
    size_t cnt = (len - off > CHUNK) ? CHUNK : (len - off);
    Wire.beginTransmission(addr);
    Wire.write(ctrl);
    for (size_t i = 0; i < cnt; ++i) Wire.write(data[off + i]);
    uint8_t r = Wire.endTransmission();
    err |= (r != 0);
    if (r != 0) return false;
    off += cnt;
    delayShort(); 
  }
  return true;
}


US2066LCD::US2066LCD() {}

bool US2066LCD::begin(int sda, int scl, int rst, uint8_t addr) {
  _addr = addr;
  _sda  = sda;
  _scl  = scl;
  _rst  = rst;

  if (_rst >= 0) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, LOW);
    delay(10);
    digitalWrite(_rst, HIGH);
    delay(10);
  }

  Wire.begin(_sda, _scl);
  setI2CClock(100000);

  _i2cError = false;

  if (!i2cSend2(_addr, CTRL_CMD, CMD_FUNCSET_RE1, _i2cError)) return false;
  if (!i2cSend2(_addr, CTRL_CMD, CMD_OLED_ON,     _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, 0xD5, _i2cError)) return false;
  if (!i2cSend2(_addr, CTRL_CMD, 0x70, _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, CMD_OLED_OFF,    _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, 0x09, _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, (uint8_t)(CMD_ENTRY_MODE | BIT_ENTRY_INC), _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, 0x72, _i2cError)) return false;
  if (!i2cSend2(_addr, CTRL_DATA, 0x00, _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, CMD_FUNCSET_RE1, _i2cError)) return false;
  if (!i2cSend2(_addr, CTRL_CMD, CMD_OLED_ON,     _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, 0xDA, _i2cError)) return false;
  if (!i2cSend2(_addr, CTRL_CMD, 0x10, _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, 0xDC, _i2cError)) return false;
  if (!i2cSend2(_addr, CTRL_CMD, 0x00, _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, 0x81, _i2cError)) return false;
  if (!i2cSend2(_addr, CTRL_CMD, 0x7F, _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, 0xD9, _i2cError)) return false;
  if (!i2cSend2(_addr, CTRL_CMD, 0xF1, _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, 0xDB, _i2cError)) return false;
  if (!i2cSend2(_addr, CTRL_CMD, 0x40, _i2cError)) return false;

  if (!i2cSend2(_addr, CTRL_CMD, CMD_OLED_OFF,    _i2cError)) return false;
  if (!i2cSend2(_addr, CTRL_CMD, CMD_FUNCSET_RE0, _i2cError)) return false;

  i2cSend2(_addr, CTRL_CMD, CMD_CLEAR, _i2cError); delayLong();
  i2cSend2(_addr, CTRL_CMD, CMD_HOME,  _i2cError); delayLong();
  i2cSend2(_addr, CTRL_CMD, (uint8_t)(CMD_DISPLAY_CTRL | BIT_DISPLAY_ON), _i2cError);

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 20; ++c) _rows_buf[r][c] = ' ';
    _rows_buf[r][20] = '\0';
  }
  _disp_on = true; _cur_on = false; _blink = false;
  _cursor_row = 0; _cursor_col = 0;
  _lastUpdateMs = millis();

  _inited = !_i2cError;
  return _inited;
}


void US2066LCD::setRowMapping(RowMappingType type) {
  switch (type) {
    case ROW_MAPPING_STANDARD:        // 0x00,0x40,0x14,0x54
      _row_addresses[0] = 0x00; _row_addresses[1] = 0x40;
      _row_addresses[2] = 0x14; _row_addresses[3] = 0x54;
      break;
    case ROW_MAPPING_SEQUENTIAL:      // 0x00,0x20,0x40,0x60 (emulator)
      _row_addresses[0] = 0x00; _row_addresses[1] = 0x20;
      _row_addresses[2] = 0x40; _row_addresses[3] = 0x60;
      break;
    case ROW_MAPPING_ALTERNATIVE:     // 0x00,0x20,0x14,0x34
      _row_addresses[0] = 0x00; _row_addresses[1] = 0x20;
      _row_addresses[2] = 0x14; _row_addresses[3] = 0x34;
      break;
    default:
      
      break;
  }
}

void US2066LCD::setCustomRowMapping(uint8_t r0, uint8_t r1, uint8_t r2, uint8_t r3) {
  _row_addresses[0] = r0;
  _row_addresses[1] = r1;
  _row_addresses[2] = r2;
  _row_addresses[3] = r3;
}

void US2066LCD::setGlobalColumnOffset(int8_t offset) {
  if (offset < -4) offset = -4;
  if (offset >  4) offset =  4;
  _global_col_offset = offset;
}

void US2066LCD::testAlignment() {
  const char* pat = "0123456789abcdefghij";
  for (uint8_t r = 0; r < _rows; ++r) {
    setCursor(0, r);
    writeRow(r, String(pat));
  }
}


void US2066LCD::command(uint8_t cmd) { writeCmd(cmd); }

size_t US2066LCD::write(uint8_t ch) {
  if (ch < 0x20 || ch > 0x7E) ch = ' ';
  setCursor(_cursor_col, _cursor_row);
  writeData(ch);
  if (_cursor_row < 4 && _cursor_col < 20) {
    _rows_buf[_cursor_row][_cursor_col] = (char)ch;
  }
  if (++_cursor_col >= _cols) {
    _cursor_col = 0;
    if (++_cursor_row >= _rows) _cursor_row = 0;
  }
  _touch();
  return 1;
}

void US2066LCD::print(const char* s) {
  if (!s) return;
  while (*s) write((uint8_t)*s++);
}

void US2066LCD::clear() {
  i2cSend2(_addr, CTRL_CMD, CMD_CLEAR, _i2cError);
  delayLong();
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 20; ++c) _rows_buf[r][c] = ' ';
  }
  _cursor_row = 0; _cursor_col = 0;
  _touch();
  _broadcast(true);                      

  for (uint8_t rr = 0; rr < 4; ++rr) {
    setCursor(0, rr);
    writeRow(rr, String(_rows_buf[rr]));
  }
}

void US2066LCD::home() {
  i2cSend2(_addr, CTRL_CMD, CMD_HOME, _i2cError);
  delayLong();
  _cursor_row = 0; _cursor_col = 0;
  _touch();
  _broadcast(true);                      

  for (uint8_t rr = 0; rr < 4; ++rr) {
    setCursor(0, rr);
    writeRow(rr, String(_rows_buf[rr]));
  }
}

void US2066LCD::displayOn(bool cursor, bool blink) {
  uint8_t cmd = CMD_DISPLAY_CTRL | BIT_DISPLAY_ON;
  if (cursor) cmd |= BIT_CURSOR_ON;
  if (blink)  cmd |= BIT_BLINK_ON;
  i2cSend2(_addr, CTRL_CMD, cmd, _i2cError);

  _disp_on = true;
  _cur_on  = cursor;
  _blink   = blink;
  _touch();
  _broadcast(true);                      

  for (uint8_t rr = 0; rr < 4; ++rr) {
    setCursor(0, rr);
    writeRow(rr, String(_rows_buf[rr]));
  }
}

void US2066LCD::setCursor(uint8_t col, uint8_t row) {
  if (row >= _rows) row = _rows - 1;
  if (col >= _cols) col = _cols - 1;

  int16_t c = (int16_t)col + (int16_t)_global_col_offset;
  if (c < 0) c = 0;
  if (c >= _cols) c = _cols - 1;

  uint8_t addr = (uint8_t)(_row_addresses[row] + (uint8_t)c);
  i2cSend2(_addr, CTRL_CMD, (uint8_t)(CMD_SET_DDRAM | addr), _i2cError);

  _cursor_row = row; _cursor_col = col;
  _touch();
}

void US2066LCD::writeRow(uint8_t row, const String& text) {
  if (row >= _rows) return;

  int16_t c0 = (int16_t)0 + (int16_t)_global_col_offset;
  if (c0 < 0) c0 = 0;
  if (c0 >= _cols) c0 = _cols - 1;

  uint8_t addr = (uint8_t)(_row_addresses[row] + (uint8_t)c0);
  i2cSend2(_addr, CTRL_CMD, (uint8_t)(CMD_SET_DDRAM | addr), _i2cError);

  const uint8_t W = _cols; 
  uint8_t buf[40]; 
  size_t n = text.length();
  for (uint8_t i = 0; i < W; ++i) {
    char ch = (i < n) ? text[i] : ' ';
    if (ch < 0x20 || ch > 0x7E) ch = ' ';
    buf[i] = (uint8_t)ch;
    if (i < 20) _rows_buf[row][i] = (char)buf[i];
  }
  if (W < 21) _rows_buf[row][W] = '\0';
  i2cSendBlock(_addr, CTRL_DATA, buf, W, _i2cError);

  _cursor_row = row;
  _cursor_col = (W ? (W - 1) : 0);
  _touch();
}


void US2066LCD::enterOledCmdSet() {
  i2cSend2(_addr, CTRL_CMD, CMD_FUNCSET_RE1, _i2cError); // RE=1
  i2cSend2(_addr, CTRL_CMD, CMD_OLED_ON,     _i2cError); // SD=1
}

void US2066LCD::exitOledCmdSet() {
  i2cSend2(_addr, CTRL_CMD, CMD_OLED_OFF,    _i2cError); // SD=0
  i2cSend2(_addr, CTRL_CMD, CMD_FUNCSET_RE0, _i2cError); // RE=0
}

bool US2066LCD::setContrast(uint8_t level) {
  if (!_inited) return false;
  if (!_contrastCapable) return false;

  bool ok = true;
  _i2cError = false;

  enterOledCmdSet();
  ok &= i2cSend2(_addr, CTRL_CMD,  CMD_SET_CONTRAST, _i2cError);
  ok &= i2cSend2(_addr, CTRL_DATA, level,            _i2cError);
  exitOledCmdSet();

  ok = ok && !_i2cError;
  if (!ok) _contrastCapable = false;
  return ok;
}


void US2066LCD::writeCmd(uint8_t c)  { i2cSend2(_addr, CTRL_CMD,  c, _i2cError); }
void US2066LCD::writeData(uint8_t d) { i2cSend2(_addr, CTRL_DATA, d, _i2cError); }

void US2066LCD::setI2CClock(uint32_t hz) {
  if (hz == 0) hz = 100000;
  Wire.setClock(hz);
}


static constexpr uint16_t LCD_MONITOR_UDP_PORT = 35182; 
static WiFiUDP s_udp;

void US2066LCD::enableTelemetry(bool on) {
  _telemetryEnabled = on;
  if (on) {
    s_udp.begin(LCD_MONITOR_UDP_PORT); 
    _lastTxMs = 0;                     
    _broadcast(true);                  

    for (uint8_t rr = 0; rr < 4; ++rr) {
      setCursor(0, rr);
      writeRow(rr, String(_rows_buf[rr]));
    }
  } else {
    s_udp.stop();
  }
}

void US2066LCD::setTelemetryIntervalMs(uint32_t ms) {
  if (ms < 100) ms = 100;
  _txIntervalMs = ms;
}

void US2066LCD::loop() {
  if (!_telemetryEnabled) return;
  uint32_t now = millis();
  bool changedRecently = (now - _lastUpdateMs) < 100; 
  bool due = (now - _lastTxMs) >= _txIntervalMs;
  if (due || changedRecently) {
    _broadcast(due || changedRecently);
  }
}

void US2066LCD::_touch() {
  _lastUpdateMs = millis();
}

void US2066LCD::_broadcast(bool /*force*/) {
  if (!_telemetryEnabled) return;

  String j;
  j.reserve(256);
  j += F("{\"type\":\"lcd20x4\",\"mode\":\"US2066\",\"addr\":\"0x");
  if (_addr < 16) j += '0';
  j += String(_addr, HEX);
  j += F("\",\"disp\":");
  j += (_disp_on ? F("true") : F("false"));
  j += F(",\"cur\":");
  j += (_cur_on ? F("true") : F("false"));
  j += F(",\"blink\":");
  j += (_blink ? F("true") : F("false"));
  j += F(",\"cursor\":{\"r\":");
  j += String(_cursor_row);
  j += F(",\"c\":");
  j += String(_cursor_col);
  j += F("},\"rows\":[");
  for (int r = 0; r < 4; ++r) {
    if (r) j += ',';
    j += '\"';
    for (int c = 0; c < 20; ++c) {
      char ch = _rows_buf[r][c];
      if (ch < 0x20 || ch > 0x7E) ch = ' ';
      if (ch == '\"' || ch == '\\') { j += '\\'; }
      j += ch;
    }
    j += '\"';
  }
  j += F("]}");

  s_udp.beginPacket(IPAddress(255,255,255,255), LCD_MONITOR_UDP_PORT);
  s_udp.write((const uint8_t*)j.c_str(), j.length());
  s_udp.endPacket();
  _lastTxMs = millis();
}
