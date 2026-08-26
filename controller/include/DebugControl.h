#pragma once
#include <array>
#include <cstdint>

enum class Item : uint8_t {
    WS_SERVER,
    BROWSER_CONTROLLER,
    SLAVE_CONTROLLER,
    MASTER_CONTROLLER,
    SUNSHADE_CONTROLLER,
    WIFI,
    NVS,
    TASK_UTILS,
    // add more as needed
    COUNT   // always last
};

class DebugControl {
public:
    static bool enabled(Item section);
    static void set(Item section, bool value);
    static void setAll(bool value);
    static const char* itemToString(Item section);

private:
    static std::array<bool, static_cast<size_t>(Item::COUNT)> flags_;
};

