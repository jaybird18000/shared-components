#pragma once
#include <array>
#include <cstdint>

enum class DebugItem : uint8_t {
    WS_SERVER,
    BROWSER_CONTROLLER,
    SLAVE_CONTROLLER,
    MASTER_CONTROLLER,
    SUNSHADE_CONTROLLER,
    WIFI,
    NVS,
    TASK_UTILS,
    MDNS,
    // add more as needed
    COUNT   // always last
};

class DebugControl {
public:
    static bool enabled(DebugItem section);
    static void set(DebugItem section, bool value);
    static void setAll(bool value);
    static void setTestMode(bool value);
    static bool testMode();
    static const char* itemToString(DebugItem section);

private:
    static std::array<bool, static_cast<size_t>(DebugItem::COUNT)> flags_;
    static bool testModeFlag_;
};

