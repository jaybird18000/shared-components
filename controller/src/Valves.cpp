#include "Valves.h"
#include "wsServer.h"
#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <string.h>

static const char* TAG = "VALVES";

static gpio_config_t createGpioConfig(uint64_t pinMask)
{
    gpio_config_t config = {};
    config.pin_bit_mask = pinMask;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    return config;
}

Valve::Valve(int openPin, int closePin, int limitOpenPin, int limitClosedPin, const char* name)
    : openPin_(openPin), closePin_(closePin), limitOpenPin_(limitOpenPin), limitClosedPin_(limitClosedPin), name_(name), state_(ValveState::Idle), actionStartUs_(0)
{
}

void Valve::init()
{
    gpio_config_t io = createGpioConfig((1ULL << openPin_) | (1ULL << closePin_));
    gpio_config(&io);
    gpio_config_t limits = {};
    limits.pin_bit_mask = (1ULL << limitOpenPin_) | (1ULL << limitClosedPin_);
    limits.mode = GPIO_MODE_INPUT;
    limits.pull_down_en = GPIO_PULLDOWN_DISABLE;
    limits.pull_up_en = GPIO_PULLUP_ENABLE;
    limits.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&limits);
    stop();
}

void Valve::open()
{
    ESP_LOGI("Valve", "%s : Attempting to open: Open pin %d, isOpenLimit %d", name_, openPin_, isOpenLimit());
    if (isOpenLimit()) {
        state_ = ValveState::Open;
        stop();
        return;
    }
    gpio_set_level((gpio_num_t)closePin_, 0);
    gpio_set_level((gpio_num_t)openPin_, 1);
    state_ = ValveState::Opening;
    actionStartUs_ = esp_timer_get_time();
}

void Valve::close()
{
    ESP_LOGI("Valve", "%s : Attempting to close: Closed pin %d, isClosedLimit %d", name_, closePin_, isClosedLimit());
    if (isClosedLimit()) {
        state_ = ValveState::Closed;
        stop();
        return;
    }
    gpio_set_level((gpio_num_t)openPin_, 0);
    gpio_set_level((gpio_num_t)closePin_, 1);
    state_ = ValveState::Closing;
    actionStartUs_ = esp_timer_get_time();
}

void Valve::stop()
{
    gpio_set_level((gpio_num_t)openPin_, 0);
    gpio_set_level((gpio_num_t)closePin_, 0);
    if (isOpenLimit()) {
        state_ = ValveState::Open;
    } else if (isClosedLimit()) {
        state_ = ValveState::Closed;
    } else if (state_ == ValveState::Opening || state_ == ValveState::Closing) {
        state_ = ValveState::Idle;
    }
}

void Valve::update()
{
//    ESP_LOGI("Valve", "%s : Updating valve state: %s", name_, statusText());
    if(isOpenLimit() && isClosedLimit()) {
        if(state_ != ValveState::Error) {
            ESP_LOGE("Valve", "%s : ERROR Both open and closed limit switches are active!", name_);
            WsServer::instance().postDebug("%s : ERROR Both open and closed limit switches are active!", name_);
        }
        state_ = ValveState::Error;
        actionStartUs_ = 0;
        gpio_set_level((gpio_num_t)openPin_, 0);
        gpio_set_level((gpio_num_t)closePin_, 0);
        return;
    }
    if (state_ == ValveState::Opening && isOpenLimit()) {
        stop();
        state_ = ValveState::Open;
        actionStartUs_ = 0;
        return;
    }
    else if (state_ == ValveState::Closing && isClosedLimit()) {
        stop();
        state_ = ValveState::Closed;
        actionStartUs_ = 0;
        return;
    }
    else if ((state_ == ValveState::Opening || state_ == ValveState::Closing) && actionStartUs_ > 0) {
        int64_t delta = esp_timer_get_time() - actionStartUs_;
        if (delta > kActionTimeoutUs) {
            stop();
            actionStartUs_ = 0;
            state_ = ValveState::Error;
        }
    }
    else if(state_ == ValveState::Idle || state_ == ValveState::Error) {
        if (isOpenLimit() && isClosedLimit())
        {
            // do nothing idle or error still active
        }
        else if (isOpenLimit()) {
            state_ = ValveState::Open;
        } else if (isClosedLimit()) {
            state_ = ValveState::Closed;
        }
    }
    else if (state_ == ValveState::Open && !isOpenLimit()) {
        state_ = ValveState::Idle;
    } 
    else if (state_ == ValveState::Closed && !isClosedLimit()) {
        state_ = ValveState::Idle;
    }
    else if(state_ == ValveState::Closed && isOpenLimit()) {
        state_ = ValveState::Open;
    } 
    else if(state_ == ValveState::Open && isClosedLimit()) {
        state_ = ValveState::Closed;
    }
}

ValveState Valve::state() const
{
    return state_;
}

const char* Valve::statusText() const
{
    switch (state_) {
    case ValveState::Idle: return "idle";
    case ValveState::Opening: return "opening";
    case ValveState::Closing: return "closing";
    case ValveState::Open: return "open";
    case ValveState::Closed: return "closed";
    case ValveState::Error: return "error";
    }
    return "unknown";
}

const char* Valve::name() const
{
    return name_;
}

bool Valve::isOpenLimit() const
{
//    ESP_LOGI("Valve", "%s: Checking open limit pin %d, value=%d", name_, limitOpenPin_, gpio_get_level((gpio_num_t)limitOpenPin_));
    return gpio_get_level((gpio_num_t)limitOpenPin_) == 1;
}

bool Valve::isClosedLimit() const
{
//    ESP_LOGI("Valve", "%s: Checking closed limit pin %d, value=%d", name_, limitClosedPin_, gpio_get_level((gpio_num_t)limitClosedPin_));
    return gpio_get_level((gpio_num_t)limitClosedPin_) == 1;
}

ValvesController::ValvesController()
    : generatorValve_(GPIO_NUM_13, GPIO_NUM_12, GPIO_NUM_16, GPIO_NUM_18, "Generator Valve"),
      acValve_(GPIO_NUM_15, GPIO_NUM_14, GPIO_NUM_3, GPIO_NUM_17, "AC Valve")
{
}

void ValvesController::init()
{
    generatorValve_.init();
    acValve_.init();
}

void ValvesController::openGenerator()
{
    generatorValve_.open();
}

void ValvesController::closeGenerator()
{
    generatorValve_.close();
}

void ValvesController::openAirConditioner()
{
    acValve_.open();
}

void ValvesController::closeAirConditioner()
{
    acValve_.close();
}

void ValvesController::update()
{
    generatorValve_.update();
    acValve_.update();
}

const Valve& ValvesController::generatorValve() const
{
    return generatorValve_;
}

const Valve& ValvesController::acValve() const
{
    return acValve_;
}
