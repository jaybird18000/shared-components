#ifndef GEN_RELAY_H
#define GEN_RELAY_H

#include <driver/gpio.h>
#include <string>
#include <esp_log.h>
#include <esp_timer.h>
#include <algorithm>
#include <string.h>
#include <stdio.h>
#include <math.h>

class GenRelay {
 
public:
    enum GenStates{
        NOT_RUNNING,
        RUNNING,
        STARTING,
        STOPPING
    };
     
    GenRelay();
    void init();
    void setState(float voltage);
    void setEnabled(bool enabled, bool start);
    bool enabled(bool start) const;
    const char* stateText() const;
    // NEW: momentary pulse (active for durationMs, then returns to previous state)
    void pulse(uint32_t durationMs, bool start);
    void momentaryStart();
    void momentaryStop();

private:
    static const char* genStateToString(GenStates s);
    float VAC_ON_TRESHOLD = 90.0;
    gpio_num_t startPin_;
    gpio_num_t stopPin_;
    bool startEnabled_;
    bool stopEnabled_;
    bool startRelayCommand;
    GenStates currentState_;
    esp_timer_handle_t startingTimer;
    esp_timer_handle_t stoppingTimer;
};

inline GenRelay::GenRelay()
    : startPin_(GPIO_NUM_12), stopPin_(GPIO_NUM_13), startEnabled_(false), stopEnabled_(false), startRelayCommand(false)
{
}
inline const char* GenRelay::genStateToString(GenStates s) {
    switch (s) {
        case NOT_RUNNING: return "NOT RUNNING";
        case RUNNING:     return "RUNNING";
        case STARTING:    return "STARTING";
        case STOPPING:    return "STOPPING";
        default:          return "UNKNOWN";
    }
}  
inline void GenRelay::init()
{
    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << startPin_) | (1ULL << stopPin_);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&config);
    // depending on the relay module, setting enable to true makes the 
    // relay active low, so we start with it off by setting enabled to true
    bool startRelay = true;
    setEnabled(false, startRelay);   
    setEnabled(false, !startRelay);   
    currentState_ = NOT_RUNNING;

    esp_timer_create_args_t args = {
        .callback = [](void* arg) {
            GenRelay* self = static_cast<GenRelay*>(arg);
            self->currentState_ = NOT_RUNNING;
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "starting_timer",
        .skip_unhandled_events = false   // NEW FIELD REQUIRED IN ESP-IDF v6
    };

    esp_timer_create(&args, &startingTimer);

    esp_timer_create_args_t args1 = {
        .callback = [](void* arg) {
            GenRelay* self = static_cast<GenRelay*>(arg);
            self->currentState_ = RUNNING;
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "stopping_timer",
        .skip_unhandled_events = false   // NEW FIELD REQUIRED IN ESP-IDF v6
    };

    esp_timer_create(&args1, &stoppingTimer);
}

inline void GenRelay::setEnabled(bool enabled, bool startRelay)
{
    if(startRelay)
    {
        startEnabled_ = enabled;
        gpio_set_level(startPin_, enabled ? 1 : 0);

    }
    else
    {
        stopEnabled_ = enabled;
        gpio_set_level(stopPin_, enabled ? 1 : 0);
    }
}

inline bool GenRelay::enabled(bool startRelay) const
{
    if(startRelay)
    {
        return startEnabled_;
    }
    else
    {
        return stopEnabled_;
    }

}

inline const char* GenRelay::stateText() const
{
    return genStateToString(currentState_);
}
inline void GenRelay::setState(float voltage)
{
    switch(currentState_)
    {
        case NOT_RUNNING:
            if(voltage > VAC_ON_TRESHOLD)
            {
                currentState_ = RUNNING;
            }
            else
            {
                currentState_ = NOT_RUNNING;
            }
            break;
        case RUNNING:
            if(voltage > VAC_ON_TRESHOLD)
            {
                currentState_ = RUNNING;
            }
            else
            {
                currentState_ = NOT_RUNNING;
            }
            break;
        case STARTING:
            if (esp_timer_is_active(startingTimer))
            {
                if(voltage > VAC_ON_TRESHOLD)
                {
                    esp_timer_stop(startingTimer);
                    currentState_ = RUNNING;
                }
            }
            else 
            {
                uint32_t durationMs = 8000;
                esp_timer_start_once(startingTimer, durationMs * 1000);
            }
            break;
        case STOPPING:
            if (esp_timer_is_active(stoppingTimer))
            {
                if(voltage < VAC_ON_TRESHOLD)
                {
                    esp_timer_stop(stoppingTimer);
                    currentState_ = NOT_RUNNING;
                }
            }
            else 
            {
                uint32_t durationMs = 5000;
                esp_timer_start_once(stoppingTimer, durationMs * 1000);
            }
            break;
            break;
        default:
            break;
    };
}
// ------------------------------------------------------------
// NEW: Momentary pulse implementation
// ------------------------------------------------------------
inline void GenRelay::pulse(uint32_t durationMs, bool startRelay)
{
    startRelayCommand = false;
    if(startRelay)
    {
        startRelayCommand = true;
    }
    // Turn relay ON momentarily (active‑LOW → LOW = ON)
    setEnabled(true, startRelay);

    // Schedule automatic return to previous state
    esp_timer_handle_t timer;
    esp_timer_create_args_t args = {
        .callback = [](void* arg) {
            GenRelay* self = static_cast<GenRelay*>(arg);
            // Restore previous state
            self->setEnabled(false, self->startRelayCommand);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "relay_pulse",
        .skip_unhandled_events = false   // NEW FIELD REQUIRED IN ESP-IDF v6
    };

    esp_timer_create(&args, &timer);
    esp_timer_start_once(timer, durationMs * 1000);
}
inline void GenRelay::momentaryStart()
{
    currentState_ = STARTING;
    pulse(2000, true); // default to 1 second pulse
}
inline void GenRelay::momentaryStop()
{
    currentState_ = STOPPING;
    pulse(2000, false); // default to 1 second pulse
}
#endif // GEN_RELAY_H
