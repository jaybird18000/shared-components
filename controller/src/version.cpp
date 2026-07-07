#include "version.h"
#include "nvsMgr.h"

std::string getMasterVersion() {
    return MASTER_FIRMWARE_VERSION;
}

std::string getSlaveVersion() {
    return SLAVE_FIRMWARE_VERSION;
}

std::string getFirmwareVersion() {
    // Check device role from NVS
    NvsMgr nvsMgr;
    bool isMaster = WifiMgr::instance().isMaster();
    
    if (isMaster) {
        return getMasterVersion();
    } else {
        return getSlaveVersion();
    }
}
