#pragma once
#include <Arduino.h>
#include <Wire.h>

// Match emulator/I2C monitor expectations for control bytes
static constexpr uint8_t CTRL_CMD  = 0x80;  // control byte for "command"
static constexpr uint8_t CTRL_DATA = 0x40;  // control byte for "data"

enum RowMappingType : uint8_t {
  ROW_MAPPING_STANDARD,        // 0x00, 0x40, 0x14, 0x54  (common HD44780 20x4)
  ROW_MAPPING_SEQUENTIAL,      // 0x00, 0x20, 0x40, 0x60  (strict 20-col rows; emulator)
  ROW_MAPPING_ALTERNATIVE,     // 0x00, 0x20, 0x14, 0x34  (some vendors)
  ROW_MAPPING_PROMETHEOS = ROW_MAPPING_SEQUENTIAL // alias for emulator/PrometheOS
};

class US2066LCD {
public:
  US2066LCD();

  bool begin(int sda, int scl, int rst = -1, uint8_t addr = 0x3C);

  // --- Alignment Configuration Methods ---
  void setRowMapping(RowMappingType type);
  void setGlobalColumnOffset(int8_t offset);  // -3 to +3 typical range
  void setCustomRowMapping(uint8_t row0, uint8_t row1, uint8_t row2, uint8_t row3);
  void testAlignment();  // Writes test pattern to verify alignment

  inline bool init(uint8_t cols=20, uint8_t rows=4, int sda=-1, int scl=-1, int rst=-1, uint8_t addr=0x3C) {
    _cols = cols; _rows = rows;
    if (sda >= 0 && scl >= 0) return begin(sda, scl, rst, addr);
    return begin(_sda, _scl, _rst, addr);
  }

  void command(uint8_t cmd);                  
  size_t write(uint8_t ch);                   
  void print(const char* s);
  void print(const String& s) { print(s.c_str()); }

  void clear();
  void home();
  void displayOn(bool cursor, bool blink);
  void noDisplay() { _disp_on=false; displayOn(false,false); }
  void cursor()    { _cur_on=true;  displayOn(true, _blink); }
  void noCursor()  { _cur_on=false; displayOn(false, _blink); }
  void blink()     { _blink=true;   displayOn(_cur_on, true); }
  void noBlink()   { _blink=false;  displayOn(_cur_on, false); }

  void setCursor(uint8_t col, uint8_t row);

  // Row writer (padded/truncated to configured cols)
  void writeRow(uint8_t row, const String& text);

  // Attempts to set drive current (0x00–0xFF). Returns false if not supported/failed.
  bool setContrast(uint8_t level);
  bool supportsContrast() const { return _contrastCapable; }

  // Optional: adjust I2C clock (defaults to 50k during begin for stability)
  void setI2CClock(uint32_t hz);

  // --- Telemetry (optional, disabled by default) ---
  void enableTelemetry(bool on);
  void setTelemetryIntervalMs(uint32_t ms);  
  void loop();                               

  // Helpers
  uint8_t address() const { return _addr; }
  int8_t  pinSDA()  const { return _sda; }
  int8_t  pinSCL()  const { return _scl; }
  int8_t  pinRST()  const { return _rst; }
  uint8_t cols()    const { return _cols; }
  uint8_t rows()    const { return _rows; }
  int8_t  globalColumnOffset() const { return _global_col_offset; }

private:
  void writeCmd(uint8_t c);
  void writeData(uint8_t d);

  // US2066 OLED command-set entry/exit used by setContrast()
  void enterOledCmdSet();   // typically: 0x2A (RE=1), 0x79 (SD=1)
  void exitOledCmdSet();    // typically: 0x78 (SD=0), 0x28 (RE=0)

  // --- State ---
  uint8_t _addr   = 0x3C;
  int8_t  _sda    = -1;
  int8_t  _scl    = -1;
  int8_t  _rst    = -1;
  bool    _inited = false;

  bool    _contrastCapable = true;
  bool    _i2cError = false;

  uint8_t _cols = 20;
  uint8_t _rows = 4;

  // --- Alignment configuration ---
  // Default to SEQUENTIAL to mirror emulator (0x00,0x20,0x40,0x60)
  uint8_t _row_addresses[4] = {0x00, 0x20, 0x40, 0x60};
  int8_t  _global_col_offset = 0;  // Global column bias

  bool     _telemetryEnabled = false;
  bool     _disp_on = true;
  bool     _cur_on  = false;
  bool     _blink   = false;
  uint8_t  _cursor_row = 0;  
  uint8_t  _cursor_col = 0;
  char     _rows_buf[4][21] = {{0}}; 
  uint32_t _lastUpdateMs = 0;
  uint32_t _lastTxMs     = 0;
  uint32_t _txIntervalMs = 1000;

  void _touch();                 
  void _broadcast(bool force);   
};
