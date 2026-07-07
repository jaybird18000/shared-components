#pragma once
#include "driver/gpio.h"
#include "esp_timer.h"
#include <stdint.h>

class LedController {
public:
    // Singleton accessor
    static LedController& instance();

    // Must be called once at startup
    void init(gpio_num_t led0, gpio_num_t led1, gpio_num_t led2);

    // Basic control
    void on(int index);
    void off(int index);
    void allOn();
    void allOff();

    // Patterns
    void blink(int index, uint32_t durationMs);
    void flash(int index, uint32_t onMs, uint32_t offMs);
    void binary(uint8_t value);
    void chase(uint32_t speedMs);

    // Stop patterns
    void stopBlink();
    void stopFlash();
    void stopChase();

    void testLeds();

private:
    // Private constructor
    LedController() = default;

    // No copying
    LedController(const LedController&) = delete;
    LedController& operator=(const LedController&) = delete;

    gpio_num_t leds_[3] = { GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC };

    esp_timer_handle_t blinkTimer_ = nullptr;
    esp_timer_handle_t flashTimer_ = nullptr;
    esp_timer_handle_t chaseTimer_ = nullptr;
};