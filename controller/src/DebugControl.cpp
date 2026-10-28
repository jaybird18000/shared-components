#include "debugControl.h"

std::array<bool, static_cast<size_t>(DebugItem::COUNT)> DebugControl::flags_ = {
    false,   // WS_SERVER
    false,   // BROWSER_CONTROLLER
    false,   // SLAVE_CONTROLLER
    false,   // MASTER_CONTROLLER
    false,   // SUNSHADE_CONTROLLER
    false,   // WIFI
    false,   // NVS
    false,   // MDNS
    false    // TASK_UTILS
};

bool DebugControl::testModeFlag_ = false;

bool DebugControl::enabled(DebugItem section) {
    return flags_[static_cast<size_t>(section)];
}

void DebugControl::set(DebugItem section, bool value) {
    flags_[static_cast<size_t>(section)] = value;
}

void DebugControl::setAll(bool value) {
    for (auto& f : flags_) f = value;
}

void DebugControl::setTestMode(bool value)
{
    testModeFlag_ = value;
}

bool DebugControl::testMode()
{
    return testModeFlag_;
}

const char* DebugControl::itemToString(DebugItem section) {
    switch (section) {
        case DebugItem::WS_SERVER:           return "WS_SERVER";
        case DebugItem::BROWSER_CONTROLLER:  return "BROWSER_CONTROLLER";
        case DebugItem::SLAVE_CONTROLLER:    return "SLAVE_CONTROLLER";
        case DebugItem::MASTER_CONTROLLER:   return "MASTER_CONTROLLER";
        case DebugItem::SUNSHADE_CONTROLLER: return "SUNSHADE_CONTROLLER";
        case DebugItem::WIFI:                return "WIFI";
        case DebugItem::NVS:                 return "NVS";
        case DebugItem::TASK_UTILS:          return "TASK_UTILS";
        case DebugItem::MDNS:                return "MDNS";
        default:                             return "UNKNOWN";
    }
}