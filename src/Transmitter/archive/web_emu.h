#pragma once
#include <Arduino.h>

namespace WebEmu {

// Start the web UI & endpoints. Call after WiFiMgr::begin().
void begin();

// Call regularly (e.g., from loop) to broadcast updates when LCD state changes.
void loop();

} // namespace WebEmu
