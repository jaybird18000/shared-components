#include "statusDb.h"
#include "deviceConfig.h"

StatusDb& StatusDb::instance() {
    static StatusDb db;
    return db;
}

StatusDb::StatusDb()
    : masterTestMode_(false),
      slaveTestMode_(false),
      sunShadeTestMode_(false),
      generatorVoltage_(0.0f),
      generatorCurrent_(0.0f),
      generatorPower_(0.0f),
      generatorFrequency_(0.0f),
      generatorValveState_(ValveState::Idle),
      generatorStatus_(GenRelay::GenStates::NOT_RUNNING),

      acValveState_(ValveState::Idle)
{
    mutex_ = xSemaphoreCreateMutex();
}

// ------------------- SETTERS -------------------
void StatusDb::setTestModeBasedOnDeviceRole(bool enabled) {
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) 
    {
        if(DeviceConfig::instance().isMasterDevice())
        {
            masterTestMode_ = enabled;
        }
        else if (DeviceConfig::instance().isSunShadeDevice())
        {
            sunShadeTestMode_ = enabled;
        }
        else
        {
            slaveTestMode_ = enabled;
        }

        xSemaphoreGive(mutex_);
    }
}
void StatusDb::setMasterTestMode(bool enabled) {
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        masterTestMode_ = enabled;
        xSemaphoreGive(mutex_);
    }
}

void StatusDb::setSlaveTestMode(bool enabled) {
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        slaveTestMode_ = enabled;
        xSemaphoreGive(mutex_);
    }
}

void StatusDb::setSunShadeTestMode(bool enabled) {
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        sunShadeTestMode_ = enabled;
        xSemaphoreGive(mutex_);
    }
}

void StatusDb::setGeneratorValveState(ValveState state)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        generatorValveState_ = state;
        xSemaphoreGive(mutex_);
    }
}

void StatusDb::setACValveState(ValveState state)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        acValveState_ = state;
        xSemaphoreGive(mutex_);
    }
}

void StatusDb::setGeneratorState(GenRelay::GenStates status)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        generatorStatus_ = status;
        xSemaphoreGive(mutex_);
    }
}

void StatusDb::setGeneratorVoltage(float voltage)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        generatorVoltage_ = voltage;
        xSemaphoreGive(mutex_);
    }
}

void StatusDb::setGeneratorCurrent(float current)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        generatorCurrent_ = current;
        xSemaphoreGive(mutex_);
    }
}

void StatusDb::setGeneratorPower(float power)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        generatorPower_ = power;
        xSemaphoreGive(mutex_);
    }
}

void StatusDb::setGeneratorFrequency(float frequency)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        generatorFrequency_ = frequency;
        xSemaphoreGive(mutex_);
    }
}

void StatusDb::setAcData(float voltage, float current, float frequency, float watts, GenRelay::GenStates genStatus)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        generatorVoltage_ = voltage;
        generatorCurrent_ = current;
        generatorFrequency_ = frequency;
        generatorPower_ = watts;
        generatorStatus_ = genStatus;
        xSemaphoreGive(mutex_);
    }
}

// ------------------- GETTERS -------------------

bool StatusDb::getMasterTestMode() {
    bool value = false;
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        value = masterTestMode_;
        xSemaphoreGive(mutex_);
    }
    return value;
}

bool StatusDb::getSlaveTestMode() {
    bool value = false;
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        value = slaveTestMode_;
        xSemaphoreGive(mutex_);
    }
    return value;
}

bool StatusDb::getSunShadeTestMode() {
    bool value = false;
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        value = sunShadeTestMode_;
        xSemaphoreGive(mutex_);
    }
    return value;
}

ValveState StatusDb::getGeneratorValveState()
{
    ValveState value = ValveState::Idle; // Assuming IDLE is a default state
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        value = generatorValveState_;
        xSemaphoreGive(mutex_);
    }
    return value;
}

ValveState StatusDb::getACValveState()
{
    ValveState value = ValveState::Idle; // Assuming IDLE is a default state
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        value = acValveState_;
        xSemaphoreGive(mutex_);
    }
    return value;
}

float StatusDb::getGeneratorVoltage()
{
    float value = 0.0f;
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        value = generatorVoltage_;
        xSemaphoreGive(mutex_);
    }
    return value;
}

float StatusDb::getGeneratorCurrent()
{
    float value = 0.0f;
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        value = generatorCurrent_;
        xSemaphoreGive(mutex_);
    }
    return value;
}

GenRelay::GenStates StatusDb::getGeneratorState()
{
    GenRelay::GenStates value = GenRelay::GenStates::NOT_RUNNING; // Assuming Off is a default state
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        value = generatorStatus_;
        xSemaphoreGive(mutex_);
    }
    return value;
}

float StatusDb::getGeneratorPower()
{
    float value = 0.0f;
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        value = generatorPower_;
        xSemaphoreGive(mutex_);
    }
    return value;
}

float StatusDb::getGeneratorFrequency()
{
    float value = 0.0f;
    if (xSemaphoreTake(mutex_, portMAX_DELAY)) {
        value = generatorFrequency_;
        xSemaphoreGive(mutex_);
    }
    return value;
}
