#include "DeviceConfig.h"
#include "nvsMgr.h"

DeviceConfig &DeviceConfig::instance()
{
    static DeviceConfig instance;
    return instance;
}

DeviceConfig::DeviceConfig()
{
    init();
}

bool DeviceConfig::isSunShadeDevice()
{
    int id = gpio_get_level(CONFIG_PIN_0);
    return id == 1;
}
bool DeviceConfig::isMasterDevice()
{
    return isMaster_;
}
bool DeviceConfig::isSlaveDevice()
{
    return !isMaster_ && !isSunShadeDevice();
}

DeviceRole DeviceConfig::getDeviceRole()
{
    if (isSunShadeDevice()) {
        return DeviceRole::SUNSHADE;
    } else if (isMasterDevice()) {
        return DeviceRole::MASTER;
    } else {
        return DeviceRole::SLAVE;
    }
}

void DeviceConfig::init()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = BIT64(CONFIG_PIN_0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,   // external pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    isMaster_ = NvsMgr::instance().currentSTA_Config().isMaster;
}

const char* toString(DeviceRole role) {
    switch (role) {
        case DeviceRole::MASTER:   return "MASTER";
        case DeviceRole::SLAVE:    return "SLAVE";
        case DeviceRole::SUNSHADE: return "SUNSHADE";
        default:                   return "UNKNOWN";
    }
}
