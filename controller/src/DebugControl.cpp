#include "debugControl.h"

std::array<bool, static_cast<size_t>(Item::COUNT)> DebugControl::flags_ = {
    false,   // WS_SERVER
    false,   // BROWSER_CONTROLLER
    false,   // SLAVE_CONTROLLER
    false,   // MASTER_CONTROLLER
    false,   // WIFI
    false,   // NVS
    false    // TASK_UTILS
};

bool DebugControl::enabled(Item section) {
    return flags_[static_cast<size_t>(section)];
}

void DebugControl::set(Item section, bool value) {
    flags_[static_cast<size_t>(section)] = value;
}

void DebugControl::setAll(bool value) {
    for (auto& f : flags_) f = value;
}

const char* DebugControl::itemToString(Item section) {
    switch (section) {
        case Item::WS_SERVER:           return "WS_SERVER";
        case Item::BROWSER_CONTROLLER:  return "BROWSER_CONTROLLER";
        case Item::SLAVE_CONTROLLER:    return "SLAVE_CONTROLLER";
        case Item::MASTER_CONTROLLER:   return "MASTER_CONTROLLER";
        case Item::SUNSHADE_CONTROLLER: return "SUNSHADE_CONTROLLER";
        case Item::WIFI:                return "WIFI";
        case Item::NVS:                 return "NVS";
        case Item::TASK_UTILS:          return "TASK_UTILS";
        default:                        return "UNKNOWN";
    }
}