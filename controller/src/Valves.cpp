#include "Valves.h"
#include "wsServer.h"
#include "DebugServices.h"
#include "DebugControl.h"
#include "statusDb.h"
#include <esp_log.h>
#include "esp_random.h"
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
    if(DebugControl::testMode())
    {
        limits.mode = GPIO_MODE_INPUT_OUTPUT;
    }
    limits.pull_down_en = GPIO_PULLDOWN_DISABLE;
    limits.pull_up_en = GPIO_PULLUP_ENABLE;
    limits.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&limits);
    if(DebugControl::testMode())
    {
        // randomize initial test mode state of limit switches
        testModeIdle_ = false;
        testModeError_ = false;
        uint32_t r = esp_random() % 10;
        if(r < 3)
        {
            testModeIdle_ = true;
        }
        else if(r < 6)
        {
            testModeError_ = true;
        }
        //testModeIdle_ = true;
        //testModeError_ = true;
        if(testModeIdle_ == true)
        {
            // both limit switches are not engaged
            gpio_set_level((gpio_num_t)limitOpenPin_, 0);
            gpio_set_level((gpio_num_t)limitClosedPin_, 0);
        }
        else if(testModeError_ == true)
        {
            // both limit switches are engaged
            gpio_set_level((gpio_num_t)limitOpenPin_, 1);
            gpio_set_level((gpio_num_t)limitClosedPin_, 1);
        }
        else
        {
            // simulate closed position
            gpio_set_level((gpio_num_t)limitOpenPin_, 0);
            gpio_set_level((gpio_num_t)limitClosedPin_, 1);            
        }
        testModeIdle_ = false;
        testModeError_ = false;
        ESP_LOGI(TAG, "%s : Initialized in Test Mode", name_);
    }
    setStateByReadingLimitSwitches();
    stop();

}

void Valve::open()
{
    ESP_LOGI(TAG, "%s : Attempting to open: Open pin %d, isOpenLimit %d", name_, openPin_, isOpenLimit());
    if (isOpenLimit()) {
        state_ = ValveState::Open;
        stop();
        return;
    }
    gpio_set_level((gpio_num_t)closePin_, 0);
    gpio_set_level((gpio_num_t)openPin_, 1);
    state_ = ValveState::Opening;
    actionStartUs_ = esp_timer_get_time();
    //ESP_LOGI(TAG, "%s : opened:  isOpenLimit %d state %s", name_, isOpenLimit(), statusText());
    if(DebugControl::testMode())
    {
        // In test mode, simulate the opening action by setting the closed limit low
        gpio_set_level((gpio_num_t)limitClosedPin_, 0);
        ESP_LOGI(TAG, "%s : TestModeAction Opening valve", name_);
    }
}

void Valve::close()
{
    ESP_LOGI(TAG, "%s : Attempting to close: Closed pin %d, isClosedLimit %d", name_, closePin_, isClosedLimit());
    if (isClosedLimit()) 
    {
        state_ = ValveState::Closed;
        stop();
        return;
    }
    gpio_set_level((gpio_num_t)openPin_, 0);
    gpio_set_level((gpio_num_t)closePin_, 1);
    state_ = ValveState::Closing;
    actionStartUs_ = esp_timer_get_time();
    //ESP_LOGI(TAG, "%s : CLosed:  isClosedLimit %d state %s", name_, isClosedLimit(), statusText());
    if(DebugControl::testMode()){
        // In test mode, simulate the closing action by setting the open limit low
        gpio_set_level((gpio_num_t)limitOpenPin_, 0);
        ESP_LOGI(TAG, "%s : TestModeAction Closing valve", name_);
    }
}

void Valve::stop()
{
    gpio_set_level((gpio_num_t)openPin_, 0);
    gpio_set_level((gpio_num_t)closePin_, 0);
    setStateByReadingLimitSwitches();
    ESP_LOGI(TAG, "%s : Stop Called:  isClosedLimit %d isOpenLimit %d state %s", name_, isClosedLimit(), isOpenLimit(), statusText());
}

void Valve::update()
{
    //ESP_LOGI(TAG, "%s : Update called current valve state: %s OL %d CL %d actionStartUs %lld", name_, statusText(), isOpenLimit(), isClosedLimit(), actionStartUs_);
    if(isOpenLimit() && isClosedLimit()) {
        if(state_ != ValveState::Error) {
            ESP_LOGE(TAG, "%s : ERROR Both open and closed limit switches are active!", name_);
            debugServices::postDebug("%s : ERROR Both open and closed limit switches are active!", name_);
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

        if(DebugControl::testMode()) 
        {
            // Test mode: simulate limit switch activation after a timeout
            // this has to happen before the action times out and the valve enters the error state
            if (delta > 7000000LL) 
            {
                if(state_ == ValveState::Opening)
                {
                    gpio_set_level((gpio_num_t)limitOpenPin_, 1);
                    ESP_LOGI(TAG, "%s : TestModeAction Opening valve timed out, setting open limit high", name_);
                }
                else
                {
                    gpio_set_level((gpio_num_t)limitClosedPin_, 1);
                    ESP_LOGI(TAG, "%s : TestModeAction Closing valve timed out, setting closed limit high", name_);
                }
            }
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
    else if (state_ == ValveState::Open && !isOpenLimit()) 
    {
        ESP_LOGI(TAG, "%s : Update Called Open limit no longer active, transitioning to Idle state", name_);
        state_ = ValveState::Idle;
    } 
    else if (state_ == ValveState::Closed && !isClosedLimit()) {
        ESP_LOGI(TAG, "%s : Update Called Closed limit no longer active, transitioning to Idle state", name_);
        state_ = ValveState::Idle;
    }
    else if(state_ == ValveState::Closed && isOpenLimit()) {
        state_ = ValveState::Open;
    } 
    else if(state_ == ValveState::Open && isClosedLimit()) {
        state_ = ValveState::Closed;
    }
}

void Valve::setStateByReadingLimitSwitches()
{
    if (isOpenLimit() && isClosedLimit()) 
    {
        state_ = ValveState::Error;
    }    
    else if (isOpenLimit()) 
    {
        state_ = ValveState::Open;
    } 
    else if (isClosedLimit()) 
    {
        state_ = ValveState::Closed;
    } 
    else 
    {
        state_ = ValveState::Idle;
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

const char* Valve::statusText(ValveState theState)
{
    switch (theState) {
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
//    ESP_LOGI(TAG, "%s: Checking open limit pin %d, value=%d", name_, limitOpenPin_, gpio_get_level((gpio_num_t)limitOpenPin_));
    return gpio_get_level((gpio_num_t)limitOpenPin_) == 1;
}

bool Valve::isClosedLimit() const
{
//    ESP_LOGI(TAG, "%s: Checking closed limit pin %d, value=%d", name_, limitClosedPin_, gpio_get_level((gpio_num_t)limitClosedPin_));
    return gpio_get_level((gpio_num_t)limitClosedPin_) == 1;
}

ValvesController& ValvesController::instance() {
    static ValvesController inst;
    return inst;
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
    StatusDb::instance().setGeneratorValveState(generatorValve_.state());
    acValve_.update();
    StatusDb::instance().setACValveState(acValve_.state());
}

const Valve& ValvesController::generatorValve() const
{
    return generatorValve_;
}

const Valve& ValvesController::acValve() const
{
    return acValve_;
}
