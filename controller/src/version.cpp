#include "version.h"
#include "DeviceConfig.h"
std::string getMasterVersion() {
    return MASTER_FIRMWARE_VERSION;
}

std::string getSlaveVersion() {
    return SLAVE_FIRMWARE_VERSION;
}

std::string getSunShadeVersion() {
    return SUNSHADE_FIRMWARE_VERSION;
}

std::string getFirmwareVersion() {
    // Check device role from NVS
    if (DeviceConfig::instance().isMasterDevice()) {
        return getMasterVersion();
    } else if (DeviceConfig::instance().isSunShadeDevice()) {
        return getSunShadeVersion();
    } else   {
        return getSlaveVersion();
    }
}
