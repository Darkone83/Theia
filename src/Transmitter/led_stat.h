#pragma once

enum class LedStatus {
    Booting,
    Portal,
    WifiConnected,
    WifiFailed,
    UdpTransmit
};

namespace LedStat {
    void begin();
    void setStatus(LedStatus status);
    void loop();
}
