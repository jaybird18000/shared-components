#pragma once
#include "driver/gpio.h"
#include "esp_timer.h"
#include <stdint.h>

enum class DeviceRole {
    MASTER,
    SLAVE,
    SUNSHADE
};

const char* toString(DeviceRole role);

class DeviceConfig {
public:
    // Singleton accessor
    static DeviceConfig& instance();

    DeviceConfig();

    // Must be called once at startup
    void init(gpio_num_t configPin0);

    bool isSunShadeDevice();

    bool isMasterDevice();

    bool isSlaveDevice();

    DeviceRole getDeviceRole();

private:

    static constexpr gpio_num_t CONFIG_PIN_0 = GPIO_NUM_5;
 
    void init();
    // No copying
    DeviceConfig(const DeviceConfig&) = delete;
    DeviceConfig& operator=(const DeviceConfig&) = delete;

    bool isMaster_;
    gpio_num_t configPins_[1] = { GPIO_NUM_NC };

};